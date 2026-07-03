/* zstore - the compressed-page store (RECLAIM_DESIGN.md, phase B).
 *
 * Stores LZ4-compressed 4 KB anonymous pages in RAM, so that cold anon
 * pages can be reclaimed without a swap device.  Blobs live in
 * size-class slots (512/1024/2048 bytes) carved out of pool pages that
 * VM keeps permanently mapped in its own address space.  A page is
 * stored only if it compresses (header included) into a 2048-byte
 * slot, guaranteeing at least a 2x net win for every stored page.
 * All-zero pages are detected by the caller and not stored at all.
 *
 * Pool pages are allocated with plain alloc_mem() + vm_mappages().
 * zstore_put() is only ever called from the reclaim path, which is
 * protected against re-entering itself (cache_freepages() busy guard),
 * so a pool-growth allocation cannot recurse into reclaim.
 */

#include <assert.h>
#include <string.h>

#include "proto.h"
#include "vm.h"
#include "region.h"
#include "glo.h"
#include "sanitycheck.h"
#include "lz4.h"

/* Size classes.  Each slot starts with a 4-byte header: 2 bytes
 * compressed length, 1 byte class index, 1 byte magic. */
#define ZS_CLASSES	3
#define ZS_HDR		4
#define ZS_MAGIC	0x5A

static const int zs_slotsize[ZS_CLASSES] = { 512, 1024, 2048 };

/* Max compressed payload we will store: must fit the largest slot. */
#define ZS_MAXPAYLOAD	(2048 - ZS_HDR)

/* Pool cap: at most 1/4 of physical memory in pool pages. */
#define ZS_POOL_MAXPAGES	((unsigned long)total_pages / 4)

/* Per-class freelist of empty slots.  A freed slot's first pointer-size
 * bytes link it into the list. */
static void *zs_freelist[ZS_CLASSES];

/* Statistics. */
static unsigned long zs_blobs = 0;		/* blobs currently stored */
static unsigned long zs_poolpages = 0;		/* pool pages allocated */
static unsigned long zs_compressed = 0;		/* pages compressed out */
static unsigned long zs_decompressed = 0;	/* pages decompressed in */

/* Scratch buffer for the compressed side of (de)compression. */
static unsigned char zs_scratch[VM_PAGE_SIZE];

/* Permanently-mapped scratch PAGE used to move page contents to/from
 * arbitrary physical frames via sys_abscopy(), instead of mapping each
 * target frame into VM's address space per operation.  The old
 * per-operation vm_mappages()/vm_unmappage() pair issued a
 * VMCTL_FLUSHTLB on every compress/decompress, i.e. a TLB shootdown
 * from deep inside the page-fault path -- expensive and, on SMP, a
 * source of intermittent stalls when another CPU was blocked in VM.
 * One page mapped once at first use avoids all of that. */
static unsigned char *zs_page;		/* VM-mapped scratch page */
static phys_bytes zs_page_phys;		/* its physical address */

/* Sentinel returned by zstore_put_phys() for an all-zero page: nothing
 * is stored (the page can simply be freed and will zero-fill on
 * re-fault), but this is distinct from NULL (= not stored, keep page). */
void *const ZSTORE_ZERO = (void *)(vir_bytes)1;

static int
zs_scratch_page_init(void)
{
	if (zs_page != NULL)
		return OK;
	if (!(zs_page = vm_allocpage(&zs_page_phys, VMP_SLAB)))
		return ENOMEM;
	return OK;
}

static int
zs_page_is_zero(void)
{
	const unsigned long *w = (const unsigned long *)zs_page;
	int i;

	for (i = 0; i < (int)(VM_PAGE_SIZE / sizeof(*w)); i++)
		if (w[i] != 0)
			return 0;
	return 1;
}

static int
zs_grow(int class)
{
	phys_bytes ph;
	unsigned char *page;
	int i, slot = zs_slotsize[class];

	if (zs_poolpages >= ZS_POOL_MAXPAGES)
		return ENOMEM;

	if ((ph = alloc_mem(1, 0)) == NO_MEM)
		return ENOMEM;

	if (!(page = vm_mappages(CLICK2ABS(ph), 1))) {
		free_mem(ph, 1);
		return ENOMEM;
	}

	zs_poolpages++;

	/* Carve the page into slots of this class. */
	for (i = 0; i + slot <= VM_PAGE_SIZE; i += slot) {
		*(void **)(page + i) = zs_freelist[class];
		zs_freelist[class] = page + i;
	}

	return OK;
}

/*
 * Compress the page at physical address 'src_phys' into the store.
 * The page is first pulled into the permanently-mapped scratch page
 * with sys_abscopy() (no per-call VM mapping / TLB flush), then
 * compressed from there.  Returns:
 *   ZSTORE_ZERO  - the page was all zeroes; nothing stored (the caller
 *                  should free the frame; it will zero-fill on re-fault)
 *   NULL         - not stored (incompressible or pool cannot grow); the
 *                  caller must keep the frame
 *   otherwise    - an opaque blob handle
 */
void *
zstore_put_phys(phys_bytes src_phys)
{
	unsigned char *slotp;
	int clen, class;

	if (zs_scratch_page_init() != OK)
		return NULL;

	if (sys_abscopy(src_phys, zs_page_phys, VM_PAGE_SIZE) != OK)
		return NULL;

	if (zs_page_is_zero())
		return ZSTORE_ZERO;

	clen = vm_lz4_compress(zs_page, VM_PAGE_SIZE, zs_scratch,
	    ZS_MAXPAYLOAD);
	if (clen <= 0)
		return NULL;	/* incompressible (for our purposes) */

	for (class = 0; class < ZS_CLASSES; class++)
		if (clen + ZS_HDR <= zs_slotsize[class])
			break;
	assert(class < ZS_CLASSES);

	if (zs_freelist[class] == NULL && zs_grow(class) != OK)
		return NULL;

	slotp = zs_freelist[class];
	zs_freelist[class] = *(void **)slotp;

	slotp[0] = (unsigned char)(clen & 0xff);
	slotp[1] = (unsigned char)((clen >> 8) & 0xff);
	slotp[2] = (unsigned char)class;
	slotp[3] = ZS_MAGIC;
	memcpy(slotp + ZS_HDR, zs_scratch, clen);

	zs_blobs++;
	zs_compressed++;

	return slotp;
}

/*
 * Decompress the blob at 'handle' into the physical frame 'dst_phys'
 * (via the scratch page + sys_abscopy) and release the blob.  Returns
 * OK, or panics on store corruption (which would mean silent data loss
 * for the process).
 */
int
zstore_get_phys(void *handle, phys_bytes dst_phys)
{
	unsigned char *slotp = handle;
	int clen, class, dlen;

	assert(slotp != NULL);
	assert(zs_page != NULL);	/* a blob exists => put_phys ran */
	if (slotp[3] != ZS_MAGIC)
		panic("zstore: bad blob magic");
	clen = slotp[0] | (slotp[1] << 8);
	class = slotp[2];
	if (class >= ZS_CLASSES || clen <= 0 ||
	    clen + ZS_HDR > zs_slotsize[class])
		panic("zstore: bad blob header");

	dlen = vm_lz4_decompress(slotp + ZS_HDR, clen, zs_page, VM_PAGE_SIZE);
	if (dlen != VM_PAGE_SIZE)
		panic("zstore: decompress failed (%d)", dlen);

	if (sys_abscopy(zs_page_phys, dst_phys, VM_PAGE_SIZE) != OK)
		panic("zstore: abscopy on decompress failed");

	zs_decompressed++;

	zstore_free(handle);

	return OK;
}

/*
 * Release a blob without reading it (process exit / unmap).
 */
void
zstore_free(void *handle)
{
	unsigned char *slotp = handle;
	int class;

	assert(slotp != NULL);
	if (slotp[3] != ZS_MAGIC)
		panic("zstore: bad blob magic on free");
	class = slotp[2];
	assert(class < ZS_CLASSES);

	slotp[3] = 0;	/* invalidate */
	*(void **)slotp = zs_freelist[class];
	zs_freelist[class] = slotp;

	assert(zs_blobs > 0);
	zs_blobs--;
}

void
zstore_get_stats(unsigned long *blobs, unsigned long *poolpages,
	unsigned long *compressed, unsigned long *decompressed)
{
	*blobs = zs_blobs;
	*poolpages = zs_poolpages;
	*compressed = zs_compressed;
	*decompressed = zs_decompressed;
}

/* Count a zero-detected page (no blob stored) as a compress-out. */
void
zstore_count_zero(void)
{
	zs_compressed++;
}
