/* swapstore - disk-backed swap for compressed pages (RECLAIM_DESIGN.md,
 * phase C).  When the RAM compressed-page pool (zstore) is full, the
 * coldest compressed blobs are written out to a dedicated raw swap
 * partition, freeing their pool pages; on fault they are read back and
 * decompressed.  Because the blobs are already LZ4-compressed, disk
 * holds <= ~2 KB per 4 KB page ("zswap writeback").
 *
 * This file (C0) is the substrate only: the swap-slot bitmap allocator
 * and the tagged blob-handle encoding, with a boot self-test.  No device
 * is configured and no I/O is performed yet (that is C1); every entry
 * point is inert until swapstore_configure() is called with a real
 * device.
 *
 * Blob handles.  A compressed blob lives either in a RAM zstore slot or
 * in an on-disk swap slot.  struct phys_block::pb_zref stays an opaque
 * void*; the store layer tells the two apart by a low-bit tag: a RAM
 * pool-slot pointer is pointer-aligned (tag 0), while a disk slot is
 * encoded as ((slot << 1) | 1) (tag 1).  This module owns the disk-slot
 * side of that encoding.
 */

#include <assert.h>
#include <string.h>

#include "proto.h"
#include "vm.h"
#include "glo.h"

/* Encoding of an on-disk blob handle (see file header). */
#define SWAP_TAG		1UL
#define SWAP_HANDLE_IS_DISK(h)	(((vir_bytes)(h) & SWAP_TAG) != 0)
#define SWAP_HANDLE_SLOT(h)	((unsigned long)((vir_bytes)(h) >> 1))
#define SWAP_MAKE_HANDLE(slot)	((void *)(vir_bytes) \
				(((unsigned long)(slot) << 1) | SWAP_TAG))

/* Swap-slot bitmap.  One bit per 4 KB device slot; 1 = in use.  The
 * bitmap is allocated when a device is configured (C1); until then the
 * store holds zero slots and never hands any out. */
static unsigned long *swap_bitmap;	/* NULL until configured */
static unsigned long swap_nslots;	/* total slots on the device */
static unsigned long swap_used;		/* slots currently in use */
static unsigned long swap_hint;		/* round-robin search cursor */

/* Statistics. */
static unsigned long swap_out_total;	/* blobs written to disk */
static unsigned long swap_in_total;	/* blobs read back from disk */

#define BITS_PER_WORD	(sizeof(unsigned long) * 8)
#define WORD_OF(slot)	((slot) / BITS_PER_WORD)
#define BIT_OF(slot)	((slot) % BITS_PER_WORD)

static int
swap_slot_test(unsigned long slot)
{
	return (swap_bitmap[WORD_OF(slot)] >> BIT_OF(slot)) & 1UL;
}

static void
swap_slot_set(unsigned long slot)
{
	swap_bitmap[WORD_OF(slot)] |= (1UL << BIT_OF(slot));
}

static void
swap_slot_clear(unsigned long slot)
{
	swap_bitmap[WORD_OF(slot)] &= ~(1UL << BIT_OF(slot));
}

/*
 * Reserve a free swap slot, or NO_SWAP_SLOT if the device is full or not
 * configured.  Round-robin from a hint so freed slots are reused without
 * always rescanning from 0.
 */
#define NO_SWAP_SLOT	((unsigned long)-1)

unsigned long
swapstore_slot_alloc(void)
{
	unsigned long i, slot;

	if (swap_bitmap == NULL || swap_used >= swap_nslots)
		return NO_SWAP_SLOT;

	for (i = 0; i < swap_nslots; i++) {
		slot = (swap_hint + i) % swap_nslots;
		if (!swap_slot_test(slot)) {
			swap_slot_set(slot);
			swap_used++;
			swap_hint = (slot + 1) % swap_nslots;
			return slot;
		}
	}
	return NO_SWAP_SLOT;	/* should not happen: used < nslots */
}

void
swapstore_slot_free(unsigned long slot)
{
	assert(swap_bitmap != NULL);
	assert(slot < swap_nslots);
	assert(swap_slot_test(slot));
	swap_slot_clear(slot);
	assert(swap_used > 0);
	swap_used--;
}

/* Handle helpers exposed to the store layer (C2/C3). */
int
swapstore_handle_is_disk(void *handle)
{
	return SWAP_HANDLE_IS_DISK(handle);
}

void *
swapstore_slot_to_handle(unsigned long slot)
{
	return SWAP_MAKE_HANDLE(slot);
}

unsigned long
swapstore_handle_to_slot(void *handle)
{
	assert(SWAP_HANDLE_IS_DISK(handle));
	return SWAP_HANDLE_SLOT(handle);
}

void
swapstore_get_stats(unsigned long *total, unsigned long *used,
	unsigned long *in, unsigned long *out)
{
	*total = swap_nslots;
	*used = swap_used;
	*in = swap_in_total;
	*out = swap_out_total;
}

/* Accounting: bumped when a blob is written to (C2) or read back from (C3)
 * the swap device, on I/O completion. */
void swapstore_count_out(void) { swap_out_total++; }
void swapstore_count_in(void)  { swap_in_total++; }

/*
 * Configure the swap area with 'nslots' 4 KB slots (phase C1): allocate
 * and zero the slot bitmap.  Called once at swapon time.  Returns OK or
 * an error; on failure the store stays unconfigured (inert).
 */
int
swapstore_configure(unsigned long nslots)
{
	unsigned long words, bytes;
	phys_bytes ph;
	void *bm;
	int pages;

	if (swap_bitmap != NULL)
		return EBUSY;
	if (nslots == 0)
		return EINVAL;

	words = (nslots + BITS_PER_WORD - 1) / BITS_PER_WORD;
	bytes = words * sizeof(unsigned long);
	pages = (int)((bytes + VM_PAGE_SIZE - 1) / VM_PAGE_SIZE);

	if ((ph = alloc_mem(pages, 0)) == NO_MEM)
		return ENOMEM;
	if (!(bm = vm_mappages(CLICK2ABS(ph), pages))) {
		free_mem(ph, pages);
		return ENOMEM;
	}
	memset(bm, 0, (size_t)pages * VM_PAGE_SIZE);

	swap_bitmap = bm;
	swap_nslots = nslots;
	swap_used = 0;
	swap_hint = 0;
	return OK;
}

/*
 * Self-test (RECLAIM_DESIGN.md, phase C0): exercise the tagged-handle
 * encoding and the slot bitmap on a small temporary device, so the
 * load-bearing encoding is validated before anything depends on it.
 * Restores the (unconfigured) global state on exit.  Returns 0 on
 * success, -1 on failure.
 */
int
swapstore_selftest(void)
{
	static unsigned long testbits[4];	/* 256 test slots */
	unsigned long saved_nslots = swap_nslots, saved_used = swap_used;
	unsigned long *saved_bitmap = swap_bitmap, saved_hint = swap_hint;
	unsigned long s, a, b, c;
	int i, rc = 0;

	/* Handle encoding: disk slots must round-trip and be distinguishable
	 * from pointer-aligned RAM handles. */
	for (i = 0; i < 20; i++) {
		unsigned long slot = (unsigned long)i * 12345UL + 7UL;
		void *h = SWAP_MAKE_HANDLE(slot);
		if (!SWAP_HANDLE_IS_DISK(h) || SWAP_HANDLE_SLOT(h) != slot) {
			rc = -1;
			goto out;
		}
	}
	/* A pointer-aligned RAM handle must read as NOT-disk. */
	if (SWAP_HANDLE_IS_DISK((void *)&testbits[0])) {
		rc = -1;
		goto out;
	}

	/* Slot bitmap: fill a small device, then free and re-alloc. */
	memset(testbits, 0, sizeof(testbits));
	swap_bitmap = testbits;
	swap_nslots = 256;
	swap_used = 0;
	swap_hint = 0;

	if (swapstore_slot_alloc() == NO_SWAP_SLOT) { rc = -1; goto out; }
	/* We just took slot 0; grab two more. */
	a = 0;
	b = swapstore_slot_alloc();
	c = swapstore_slot_alloc();
	if (b == NO_SWAP_SLOT || c == NO_SWAP_SLOT || b == a || c == a ||
	    b == c) {
		rc = -1;
		goto out;
	}

	/* Exhaust the rest; every slot must be distinct and in range. */
	for (s = swap_used; s < swap_nslots; s++)
		if (swapstore_slot_alloc() == NO_SWAP_SLOT) { rc = -1; goto out; }
	if (swap_used != swap_nslots ||
	    swapstore_slot_alloc() != NO_SWAP_SLOT) {
		rc = -1;
		goto out;
	}

	/* Free two and confirm exactly two allocations succeed again. */
	swapstore_slot_free(b);
	swapstore_slot_free(c);
	if (swapstore_slot_alloc() == NO_SWAP_SLOT ||
	    swapstore_slot_alloc() == NO_SWAP_SLOT ||
	    swapstore_slot_alloc() != NO_SWAP_SLOT) {
		rc = -1;
		goto out;
	}

out:
	swap_bitmap = saved_bitmap;
	swap_nslots = saved_nslots;
	swap_used = saved_used;
	swap_hint = saved_hint;
	return rc;
}
