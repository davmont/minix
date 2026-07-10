/* This file is concerned with allocating and freeing arbitrary-size blocks of
 * physical memory.
 */

#define _SYSTEM 1

#include <minix/com.h>
#include <minix/callnr.h>
#include <minix/type.h>
#include <minix/config.h>
#include <minix/const.h>
#include <minix/sysutil.h>
#include <minix/syslib.h>
#include <minix/debug.h>
#include <minix/bitmap.h>

#include <sys/mman.h>

#include <limits.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>
#include <memory.h>

#include "vm.h"
#include "proto.h"
#include "util.h"
#include "glo.h"
#include "sanitycheck.h"
#include "memlist.h"

/*
 * Bitmap describing free physical pages.  Sized for a static maximum
 * (one bit per page).  Actual usage is bounded at runtime by
 * number_physical_pages, set in mem_init from the kernel-provided
 * memmap; chunks above the bitmap range are silently truncated.
 *
 * The 256 GB ceiling costs 8 MB of VM BSS (256 GB / 4 KB / 8).  That
 * grows VM's PD footprint enough to require more pt_ptalloc spare
 * pages — see STATIC_SPAREPAGES in pagetable.c.  Bumping past 256 GB
 * means revisiting both numbers together.
 */
#define MAX_PHYSICAL_MEMORY (256ULL * 1024 * 1024 * 1024)
#define MAX_NUMBER_PHYSICAL_PAGES ((int)(MAX_PHYSICAL_MEMORY/VM_PAGE_SIZE))
#define MAX_PAGE_BITMAP_CHUNKS BITMAP_CHUNKS(MAX_NUMBER_PHYSICAL_PAGES)
static bitchunk_t free_pages_bitmap[MAX_PAGE_BITMAP_CHUNKS];

/* Highest page index + 1 the system actually has.  Set in mem_init from
 * the kernel-provided memmap; capped at MAX_NUMBER_PHYSICAL_PAGES. */
static int number_physical_pages = 0;

#define PAGE_CACHE_MAX 10000
static int free_page_cache[PAGE_CACHE_MAX];
static int free_page_cache_size = 0;

/*
 * Page-reclaim watermarks and instrumentation (A0/A2; RECLAIM_DESIGN.md).
 *
 * free_page_count is a running count of set bits in free_pages_bitmap,
 * maintained at the two single choke points that flip bits
 * (alloc_pages/free_pages), so the allocator can compare against the
 * watermarks in O(1) instead of walking the bitmap (memstats).
 *
 * A2, proactive batched reclaim: when an allocation takes the free-page
 * count below the low watermark, reclaim a batch of clean cache pages
 * up to the high watermark right away, instead of letting every
 * subsequent allocation fail (a full-bitmap findbit scan) and then
 * reclaim exactly one request's worth via the NO_MEM retry in
 * alloc_mem().  A0 measured that reactive-only mode at ~38k
 * fail+reclaim+retry rounds for a single 60 MB workload, with free
 * memory pinned at ~0 the whole time.  Hysteresis: one batch per
 * below-low episode; free_pages() re-arms once free rises to the high
 * watermark.  The batch is capped so a single VM message never does
 * unbounded reclaim work; the (also batched) hard-failure path in
 * alloc_mem() remains as the backstop when the cap or an empty cache
 * leaves the episode unresolved.
 */
static unsigned long free_page_count = 0;
static unsigned long stat_alloc_fails = 0;	/* NO_MEM after reclaim */
static unsigned long stat_lowwater_hits = 0;	/* low-watermark crossings */
static int below_low_watermark = 0;		/* hysteresis state */

#define RECLAIM_WATER_LOW \
	((unsigned long)(total_pages / 100 > 512 ? total_pages / 100 : 512))
#define RECLAIM_WATER_HIGH	(2 * RECLAIM_WATER_LOW)
/* Critical watermark: below this, free is about to hit zero and the
 * hard-failure compressor can no longer bootstrap (storing a compressed
 * blob needs a zstore pool page, and growing the pool needs a free page).
 * While critical we proactively compress ANY anonymous page - not just
 * cold ones - on every allocation, unlatched, so the pool grows and RAM
 * frees while pages still remain.  Each compressed page nets positive
 * (frees ~4 KB, spends ~1 KB of pool), so free recovers above the
 * critical band and never reaches the deadlock point.  This is what makes
 * anonymous overcommit reach disk swap (phase C2/C3) instead of ENOMEM. */
#define RECLAIM_WATER_CRIT	(RECLAIM_WATER_LOW / 2)
/* OOM reserve: the last OOM_RESERVE physical pages are withheld from
 * user-process allocations (PAF_USERMEM), so that system services - init,
 * PM, VFS, file servers, drivers - can always fault in a page and keep the
 * system alive when a user memory hog has exhausted the rest.  Reclaim and
 * system allocations are not tagged PAF_USERMEM and so bypass the reserve. */
#define OOM_RESERVE \
	((unsigned long)(total_pages / 64 > 1024 ? total_pages / 64 : 1024))
/* OOM killer: sustained allocations below the reserve before the largest
 * user hog is killed.  High enough that recoverable pressure (which climbs
 * back to the high watermark and resets the counter) never trips it. */
#define OOM_PRESSURE_LIMIT	8192
static unsigned long oom_pressure = 0;	/* consecutive-ish below-reserve allocs */
static int oom_kill_wanted = 0;		/* main loop should kill a hog */
#define RECLAIM_BATCH_MAX	4096	/* pages (16 MB) per reclaim batch */
#define RECLAIM_COMPRESS_MAX	512	/* cap for a proactive compress batch
					 * (B2): compression costs CPU per
					 * page, so bound the per-alloc stall */

/* How many pages to ask cache_freepages() for in order to restore the
 * free-page headroom to the high watermark, bounded by the batch cap;
 * at least 'minimum' (the current request) so the hard-failure path
 * always asks for enough to make its retry succeed.
 */
static int reclaim_batch_size(unsigned long minimum)
{
	unsigned long want = minimum;

	if(free_page_count < RECLAIM_WATER_HIGH) {
		unsigned long batch = RECLAIM_WATER_HIGH - free_page_count;
		if(batch > RECLAIM_BATCH_MAX)
			batch = RECLAIM_BATCH_MAX;
		if(batch > want)
			want = batch;
	}

	return (int)want;
}

/* Used for sanity check. */
static phys_bytes mem_low, mem_high;

static void free_pages(phys_bytes addr, int pages);
static phys_bytes alloc_pages(int pages, int flags);

#if SANITYCHECKS
struct {
	int used;
	const char *file;
	int line;
} pagemap[MAX_NUMBER_PHYSICAL_PAGES];
#endif

#define page_isfree(i) GET_BIT(free_pages_bitmap, i)

#define RESERVEDMAGIC		0x6e4c74d5
#define MAXRESERVEDPAGES	300
#define MAXRESERVEDQUEUES	 15

static struct reserved_pages {
	struct reserved_pages *next;	/* next in use */
	int max_available;	/* queue depth use, 0 if not in use at all */
	int npages;		/* number of consecutive pages */
	int mappedin;		/* must reserved pages also be mapped? */
	int n_available;	/* number of queue entries */
	int allocflags;		/* allocflags for alloc_mem */
	struct reserved_pageslot {
		phys_bytes	phys;
		void		*vir;
	} slots[MAXRESERVEDPAGES];
	u32_t magic;
} reservedqueues[MAXRESERVEDQUEUES], *first_reserved_inuse = NULL;

int missing_spares = 0;

static void sanitycheck_queues(void)
{
	struct reserved_pages *mrq;
	int m = 0;

	for(mrq = first_reserved_inuse; mrq; mrq = mrq->next) {
		assert(mrq->max_available > 0);
		assert(mrq->max_available >= mrq->n_available);
		m += mrq->max_available - mrq->n_available;
	}

	assert(m == missing_spares);
}

static void sanitycheck_rq(struct reserved_pages *rq)
{
	assert(rq->magic == RESERVEDMAGIC);
	assert(rq->n_available >= 0);
	assert(rq->n_available <= MAXRESERVEDPAGES);
	assert(rq->n_available <= rq->max_available);

	sanitycheck_queues();
}

void *reservedqueue_new(int max_available, int npages, int mapped, int allocflags)
{
	int r;
	struct reserved_pages *rq;

	assert(max_available > 0);
	assert(max_available < MAXRESERVEDPAGES);
	assert(npages > 0);
	assert(npages < 10);

	for(r = 0; r < MAXRESERVEDQUEUES; r++)
		if(!reservedqueues[r].max_available)
			break;

	if(r >= MAXRESERVEDQUEUES) {
		printf("VM: %d reserved queues in use\n", MAXRESERVEDQUEUES);
		return NULL;
	}

	rq = &reservedqueues[r];

	memset(rq, 0, sizeof(*rq));
	rq->next = first_reserved_inuse;
	first_reserved_inuse = rq;

	rq->max_available = max_available;
	rq->npages = npages;
	rq->mappedin = mapped;
	rq->allocflags = allocflags;
	rq->magic = RESERVEDMAGIC;

	missing_spares += max_available;

	return rq;
}

static void
reservedqueue_fillslot(struct reserved_pages *rq,
	struct reserved_pageslot *rps, phys_bytes ph, void *vir)
{
	rps->phys = ph;
	rps->vir = vir;
	assert(missing_spares > 0);
	if(rq->mappedin) assert(vir);
	missing_spares--;
	rq->n_available++;
}

static int
reservedqueue_addslot(struct reserved_pages *rq)
{
	phys_bytes cl, cl_addr;
	void *vir;
	struct reserved_pageslot *rps;

	sanitycheck_rq(rq);

	if((cl = alloc_mem(rq->npages, rq->allocflags)) == NO_MEM)
		return ENOMEM;

	cl_addr = CLICK2ABS(cl);

	vir = NULL;

	if(rq->mappedin) {
		if(!(vir = vm_mappages(cl_addr, rq->npages))) {
			free_mem(cl, rq->npages);
			printf("reservedqueue_addslot: vm_mappages failed\n");
			return ENOMEM;
		}
	}

	rps = &rq->slots[rq->n_available];

	reservedqueue_fillslot(rq, rps, cl_addr, vir);

	return OK;
}

void reservedqueue_add(void *rq_v, void *vir, phys_bytes ph)
{
	struct reserved_pages *rq = rq_v;
	struct reserved_pageslot *rps;

	sanitycheck_rq(rq);

	rps = &rq->slots[rq->n_available];

	reservedqueue_fillslot(rq, rps, ph, vir);
}

static int reservedqueue_fill(void *rq_v)
{
	struct reserved_pages *rq = rq_v;
	int r;

	sanitycheck_rq(rq);

	while(rq->n_available < rq->max_available)
		if((r=reservedqueue_addslot(rq)) != OK)
			return r;

	return OK;
}

int
reservedqueue_alloc(void *rq_v, phys_bytes *ph, void **vir)
{
	struct reserved_pages *rq = rq_v;
	struct reserved_pageslot *rps;

	sanitycheck_rq(rq);

	if(rq->n_available < 1) return ENOMEM;

	rq->n_available--;
	missing_spares++;
	rps = &rq->slots[rq->n_available];

	*ph = rps->phys;
	*vir = rps->vir;

	sanitycheck_rq(rq);

	return OK;
}

void alloc_cycle(void)
{
	struct reserved_pages *rq;
	sanitycheck_queues();
	for(rq = first_reserved_inuse; rq && missing_spares > 0; rq = rq->next) {
		sanitycheck_rq(rq);
		reservedqueue_fill(rq);
		sanitycheck_rq(rq);
	}
	sanitycheck_queues();
}

/*===========================================================================*
 *				alloc_mem				     *
 *===========================================================================*/
phys_clicks alloc_mem(phys_clicks clicks, u32_t memflags)
{
/* Allocate a block of memory from the free list using first fit. The block
 * consists of a sequence of contiguous bytes, whose length in clicks is
 * given by 'clicks'.  A pointer to the block is returned.  The block is
 * always on a click boundary.  This procedure is called when memory is
 * needed for FORK or EXEC.
 */
  phys_clicks mem = NO_MEM, align_clicks = 0;

  if(memflags & PAF_ALIGN64K) {
  	align_clicks = (64 * 1024) / CLICK_SIZE;
	clicks += align_clicks;
  } else if(memflags & PAF_ALIGN16K) {
	align_clicks = (16 * 1024) / CLICK_SIZE;
	clicks += align_clicks;
  }

  /* On hard failure, reclaim a batch toward the high watermark (at
   * least the request size) rather than exactly the request: under
   * sustained pressure this amortizes one LRU sweep over many future
   * allocations instead of paying a failed full-bitmap scan plus a
   * one-request reclaim for each of them (A2; RECLAIM_DESIGN.md).
   */
  do {
	mem = alloc_pages(clicks, memflags);
  } while(mem == NO_MEM &&
	cache_freepages(reclaim_batch_size(clicks), 1 /*evict*/) > 0);

  if(mem == NO_MEM) {
	stat_alloc_fails++;
  	return mem;
  }

  if(align_clicks) {
  	phys_clicks o;
  	o = mem % align_clicks;
  	if(o > 0) {
  		phys_clicks e;
  		e = align_clicks - o;
	  	free_mem(mem, e);
	  	mem += e;
	}
  }

  return mem;
}

void mem_add_total_pages(int pages)
{
	total_pages += pages;
}

/*===========================================================================*
 *				free_mem				     *
 *===========================================================================*/
void free_mem(phys_clicks base, phys_clicks clicks)
{
/* Return a block of free memory to the hole list.  The parameters tell where
 * the block starts in physical memory and how big it is.  The block is added
 * to the hole list.  If it is contiguous with an existing hole on either end,
 * it is merged with the hole or holes.
 */
  if (clicks == 0) return;

  assert(CLICK_SIZE == VM_PAGE_SIZE);
  free_pages(base, clicks);
  return;
}

/*===========================================================================*
 *				mem_init				     *
 *===========================================================================*/
void mem_init(struct memory *chunks)
{
/* Initialize hole lists.  There are two lists: 'hole_head' points to a linked
 * list of all the holes (unused memory) in the system; 'free_slots' points to
 * a linked list of table entries that are not in use.  Initially, the former
 * list has one entry for each chunk of physical memory, and the second
 * list links together the remaining table slots.  As memory becomes more
 * fragmented in the course of time (i.e., the initial big holes break up into
 * smaller holes), new table slots are needed to represent them.  These slots
 * are taken from the list headed by 'free_slots'.
 */
  int i, first = 0;
  phys_bytes top_page = 0;

  total_pages = 0;

  /*
   * Discover the highest physical address from the chunks before
   * populating the bitmap.  free_pages() indexes the bitmap by page
   * number, and a chunk whose top page is >= number_physical_pages
   * would silently write past the bitmap and corrupt adjacent BSS
   * (this is what trips the level >= 1 assert in vm_allocpages on
   * systems with > 4 GB RAM if the limit is left at 4 GB).
   *
   * Work in page units (phys_bytes, 64-bit on amd64), not bytes:
   * CLICK2ABS on a phys_clicks (uint32) page count overflows once
   * the count exceeds 1 M (= 4 GB worth of pages).
   */
  for (i = 0; i < NR_MEMS; i++) {
  	if (chunks[i].size > 0) {
		phys_bytes top = (phys_bytes)chunks[i].base
			+ (phys_bytes)chunks[i].size;
		if (top > top_page) top_page = top;
	}
  }
  number_physical_pages = (top_page > (phys_bytes)INT_MAX)
		? INT_MAX : (int)top_page;
  if (number_physical_pages > MAX_NUMBER_PHYSICAL_PAGES) {
	printf("VM: warning: physical memory top page 0x%lx exceeds bitmap "
		"max %lluGB; capping (bump MAX_PHYSICAL_MEMORY in alloc.c)\n",
		(unsigned long)top_page,
		(unsigned long long)(MAX_PHYSICAL_MEMORY >> 30));
	number_physical_pages = MAX_NUMBER_PHYSICAL_PAGES;
  }

  memset(free_pages_bitmap, 0, sizeof(free_pages_bitmap));

  /* Use the chunks of physical memory to allocate holes. */
  for (i=NR_MEMS-1; i>=0; i--) {
  	if (chunks[i].size > 0) {
		phys_clicks base = chunks[i].base, size = chunks[i].size;
		phys_bytes from, to;

		/* Skip / truncate any portion above the bitmap range. */
		if ((int)base >= number_physical_pages) continue;
		if ((int)(base + size) > number_physical_pages)
			size = number_physical_pages - (int)base;

		from = CLICK2ABS(base);
		to = CLICK2ABS(base + size) - 1;
		if(first || from < mem_low) mem_low = from;
		if(first || to > mem_high) mem_high = to;
		free_mem(base, size);
		total_pages += size;
		first = 0;
	}
  }
}

#if SANITYCHECKS
void mem_sanitycheck(const char *file, int line)
{
	int i;
	for(i = 0; i < number_physical_pages; i++) {
		if(!page_isfree(i)) continue;
		MYASSERT(usedpages_add(i * VM_PAGE_SIZE, VM_PAGE_SIZE) == OK);
	}
}
#endif

void memstats(int *nodes, int *pages, int *largest)
{
	int i;
	*nodes = 0;
	*pages = 0;
	*largest = 0;

	for(i = 0; i < number_physical_pages; i++) {
		int size = 0;
		while(i < number_physical_pages && page_isfree(i)) {
			size++;
			i++;
		}
		if(size == 0) continue;
		(*nodes)++;
		(*pages)+= size;
		if(size > *largest)
			*largest = size;
	}
}

/*
 * Returns a page number, or NO_MEM if no suitable run was found.  The return
 * type must be phys_clicks (not int): NO_MEM is (phys_clicks)-1, and callers
 * assign the result to a 64-bit phys_bytes.  An int return would sign-extend
 * the NO_MEM value to 0xffffffffffffffff, which then fails to compare equal
 * to NO_MEM (0x00000000ffffffff) and slips past the caller's error check.
 */
static phys_clicks findbit(int low, int startscan, int pages, int memflags,
	int *len)
{
	int run_length = 0, i;
	int freerange_start = startscan;

	for(i = startscan; i >= low; i--) {
		if(!page_isfree(i)) {
			int pi;
			int chunk = i/BITCHUNK_BITS, moved = 0;
			run_length = 0;
			pi = i;
			while(chunk > 0 &&
			   !MAP_CHUNK(free_pages_bitmap, chunk*BITCHUNK_BITS)) {
				chunk--;
				moved = 1;
			}
			if(moved) { i = chunk * BITCHUNK_BITS + BITCHUNK_BITS; }
			continue;
		}
		if(!run_length) { freerange_start = i; run_length = 1; }
		else { freerange_start--; run_length++; }
		assert(run_length <= pages);
		if(run_length == pages) {
			/* good block found! */
			*len = run_length;
			return freerange_start;
		}
	}

	return NO_MEM;
}

/*===========================================================================*
 *				alloc_pages				     *
 *===========================================================================*/
static phys_bytes alloc_pages(int pages, int memflags)
{
	phys_bytes boundary16 = 16 * 1024 * 1024 / VM_PAGE_SIZE;
	phys_bytes boundary1  =  1 * 1024 * 1024 / VM_PAGE_SIZE;
	phys_bytes mem = NO_MEM, i;	/* page number */
	int maxpage = number_physical_pages - 1;
	static int lastscan = -1;
	int startscan, run_length;

	/* OOM reserve: a user-process allocation may not draw the free pool
	 * below OOM_RESERVE.  Fail here so alloc_mem()'s retry loop reclaims
	 * (compress/evict/swap) and retries; if reclaim cannot lift free back
	 * above the reserve, the user allocation fails - sacrificing that user
	 * process - while the reserve stays available to system services. */
	if((memflags & PAF_USERMEM) &&
		free_page_count < OOM_RESERVE + (unsigned long)pages)
		return NO_MEM;

	if(memflags & PAF_LOWER16MB)
		maxpage = boundary16 - 1;
	else if(memflags & PAF_LOWER1MB)
		maxpage = boundary1 - 1;
	else {
		/* no position restrictions: check page cache */
		if(pages == 1) {
			while(free_page_cache_size > 0) {
				i = free_page_cache[free_page_cache_size-1];
				if(page_isfree(i)) {
					free_page_cache_size--;
					mem = i;
					assert(mem != NO_MEM);
					run_length = 1;
					break;
				}
				free_page_cache_size--;
			}
		}
	}

	if(lastscan < maxpage && lastscan >= 0)
		startscan = lastscan;
	else	startscan = maxpage;

	if(mem == NO_MEM)
		mem = findbit(0, startscan, pages, memflags, &run_length);
	if(mem == NO_MEM)
		mem = findbit(0, maxpage, pages, memflags, &run_length);
	if(mem == NO_MEM)
		return NO_MEM;

	/* remember for next time */
	lastscan = mem;

	for(i = mem; i < mem + pages; i++) {
		UNSET_BIT(free_pages_bitmap, i);
	}

	assert(free_page_count >= (unsigned long)pages);
	free_page_count -= pages;

	/* Low-watermark crossing?  Proactively reclaim a batch of clean
	 * cache pages toward the high watermark (A2), so allocations keep
	 * finding free pages instead of each one failing into the NO_MEM
	 * retry path.  One batch per episode: cache_freepages() frees via
	 * free_pages(), which re-arms the episode once the high watermark
	 * is reached.  Safe here: the reclaim path only frees (rmcache ->
	 * free_mem), it never allocates, so it cannot recurse into us.
	 */
	if(!below_low_watermark && free_page_count < RECLAIM_WATER_LOW) {
		static int reported;

		below_low_watermark = 1;
		stat_lowwater_hits++;
		if(!reported) {
			reported = 1;
			printf("VM: free pages %lu below low watermark %lu "
				"(total %d); reclaiming (first episode; "
				"further ones counted only)\n",
				free_page_count, RECLAIM_WATER_LOW,
				total_pages);
		}
		cache_freepages(reclaim_batch_size(0), 0 /*no evict*/);

		/* B2 (RECLAIM_DESIGN.md): if freeing clean cache pages did
		 * not restore headroom, proactively compress a bounded batch
		 * of COLD anonymous pages into the zstore (mode 2) rather
		 * than waiting for a hard allocation failure.  Only cold
		 * pages are taken, and the batch is capped so this cannot add
		 * a long stall to the allocation in flight.  The
		 * below_low_watermark latch (re-armed at the high watermark)
		 * keeps the pool-page allocations inside this compression
		 * from re-triggering it. */
		if(free_page_count < RECLAIM_WATER_HIGH) {
			int batch = reclaim_batch_size(0);
			if(batch > RECLAIM_COMPRESS_MAX)
				batch = RECLAIM_COMPRESS_MAX;
			cache_freepages(batch, 2 /*compress cold*/);
		}

	}

	/* Critical watermark (RECLAIM_DESIGN.md, phase C forward progress):
	 * when free is critically low, compress ANY anonymous page into the
	 * zstore now, regardless of the accessed bit and regardless of the
	 * once-per-episode low-watermark latch.  Unlike the cold-only
	 * proactive pass above, this guarantees the pool is populated and RAM
	 * is freed while pages still remain to grow the pool - so a large
	 * MAP_PREALLOC / sustained anonymous allocation never reaches the
	 * free==0 point where the hard-failure compressor cannot bootstrap.
	 * cache_freepages()'s busy guard makes the nested pool-page
	 * allocations here safe against recursion; mode 1 (evict + compress
	 * cold-then-any) takes hot pages in its second pass.  The batch is
	 * bounded and the branch self-limits: one batch frees more than it
	 * spends, lifting free back above the critical band. */
	if(free_page_count < RECLAIM_WATER_CRIT)
		cache_freepages(RECLAIM_COMPRESS_MAX, 1 /*evict + compress any*/);

	/* OOM detection: when a working set persistently exceeds RAM + swap,
	 * reclaim keeps freeing but free never recovers - the system thrashes
	 * instead of failing.  Count sustained time below the reserve; if it
	 * stays there for OOM_PRESSURE_LIMIT allocations without ever climbing
	 * back to the high watermark, flag the main loop to kill the largest
	 * user memory hog (vm_oom_kill).  Recovery to the high watermark resets
	 * the counter, so recoverable pressure (reclaim keeping pace) never
	 * trips it. */
	if(free_page_count < OOM_RESERVE) {
		if(++oom_pressure >= OOM_PRESSURE_LIMIT)
			oom_kill_wanted = 1;
	} else if(free_page_count >= RECLAIM_WATER_HIGH) {
		oom_pressure = 0;
	}

	if(memflags & PAF_CLEAR) {
		int s;
		if ((s= sys_memset(NONE, 0, CLICK_SIZE*mem,
			VM_PAGE_SIZE*pages)) != OK) 
			panic("alloc_mem: sys_memset failed: %d", s);
	}

	return mem;
}

/*===========================================================================*
 *				vm_reclaim_active			     *
 *===========================================================================*/
/* True while free memory is below the low watermark (a reclaim episode is in
 * progress).  VM's main loop uses this to drive phase-C2 swap write-back off
 * the hot allocation path. */
int vm_reclaim_active(void)
{
	return below_low_watermark;
}

/*===========================================================================*
 *				vm_oom_wanted / vm_oom_kill		     *
 *===========================================================================*/
/* Set by alloc_pages() when memory has stayed critically low long enough that
 * the system is thrashing on a working set larger than RAM + swap.  The main
 * loop polls vm_oom_wanted() and, at a safe point (never inside an allocation
 * or reclaim), calls vm_oom_kill() to SIGKILL the largest user memory hog.
 * Killing the hog is what actually reduces demand below capacity; reclaim
 * alone cannot.  System services are never chosen (acl_is_user_proc), and
 * they keep their pages via the OOM reserve, so the system stays alive. */
int vm_oom_wanted(void)
{
	return oom_kill_wanted;
}

void vm_oom_kill(void)
{
	int p, victim = -1, s;
	vir_bytes maxrss = 0;

	oom_kill_wanted = 0;
	oom_pressure = 0;		/* cooldown: let the kill free memory */

	for(p = 0; p < VMP_NR; p++) {
		struct vmproc *vmp = &vmproc[p];
		if(!(vmp->vm_flags & VMF_INUSE)) continue;
		if(vmp->vm_flags & VMF_EXITING) continue;
		if(!acl_is_user_proc(vmp)) continue;	/* protect system procs */
		if(vmp->vm_total > maxrss) {
			maxrss = vmp->vm_total;
			victim = p;
		}
	}

	if(victim < 0)
		return;			/* nothing killable (only system procs) */

	printf("VM: OOM: killing process (endpoint %d, %lu KB) to reclaim "
		"memory\n", vmproc[victim].vm_endpoint,
		(unsigned long)(vmproc[victim].vm_total / 1024));

	if((s = sys_kill(vmproc[victim].vm_endpoint, SIGKILL)) != OK)
		printf("VM: OOM: sys_kill(%d) failed: %d\n",
			vmproc[victim].vm_endpoint, s);
}

/*===========================================================================*
 *				free_pages				     *
 *===========================================================================*/
static void free_pages(phys_bytes pageno, int npages)
{
	int i, lim = pageno + npages - 1;

#if JUNKFREE
       if(sys_memset(NONE, 0xa5a5a5a5, VM_PAGE_SIZE * pageno,
               VM_PAGE_SIZE * npages) != OK)
                       panic("free_pages: sys_memset failed");
#endif

	for(i = pageno; i <= lim; i++) {
		SET_BIT(free_pages_bitmap, i);
		if(free_page_cache_size < PAGE_CACHE_MAX) {
			free_page_cache[free_page_cache_size++] = i;
		}
	}

	free_page_count += npages;

	/* Enough recovered?  Re-arm the low-watermark episode. */
	if(below_low_watermark && free_page_count >= RECLAIM_WATER_HIGH)
		below_low_watermark = 0;
}

/*===========================================================================*
 *				get_reclaim_stats_info			     *
 *===========================================================================*/
void get_reclaim_stats_info(struct vm_stats_info *vsi)
{
	vsi->vsi_alloc_fails = stat_alloc_fails;
	vsi->vsi_lowwater_hits = stat_lowwater_hits;
	vsi->vsi_water_low = RECLAIM_WATER_LOW;
	vsi->vsi_water_high = RECLAIM_WATER_HIGH;
}

/*===========================================================================*
 *				printmemstats				     *
 *===========================================================================*/
void printmemstats(void)
{
	int nodes, pages, largest;
        memstats(&nodes, &pages, &largest);
        printf("%d blocks, %d pages (%lukB) free, largest %d pages (%lukB)\n",
                nodes, pages, (unsigned long) pages * (VM_PAGE_SIZE/1024),
		largest, (unsigned long) largest * (VM_PAGE_SIZE/1024));
}


#if SANITYCHECKS

/*===========================================================================*
 *				usedpages_reset				     *
 *===========================================================================*/
void usedpages_reset(void)
{
	memset(pagemap, 0, sizeof(pagemap));
}

/*===========================================================================*
 *				usedpages_add				     *
 *===========================================================================*/
int usedpages_add_f(phys_bytes addr, phys_bytes len, const char *file, int line)
{
	u32_t pagestart, pages;

	if(!incheck)
		return OK;

	assert(!(addr % VM_PAGE_SIZE));
	assert(!(len % VM_PAGE_SIZE));
	assert(len > 0);

	pagestart = addr / VM_PAGE_SIZE;
	pages = len / VM_PAGE_SIZE;

	while(pages > 0) {
		phys_bytes thisaddr;
		assert(pagestart > 0);
		assert(pagestart < number_physical_pages);
		thisaddr = pagestart * VM_PAGE_SIZE;
		assert(pagestart < number_physical_pages);
		if(pagemap[pagestart].used) {
			static int warnings = 0;
			if(warnings++ < 100)
				printf("%s:%d: usedpages_add: addr 0x%lx reused, first %s:%d\n",
					file, line, thisaddr, pagemap[pagestart].file, pagemap[pagestart].line);
			util_stacktrace();
			return EFAULT;
		}
		pagemap[pagestart].used = 1;
		pagemap[pagestart].file = file;
		pagemap[pagestart].line = line;
		pages--;
		pagestart++;
	}

	return OK;
}

#endif

