/* swappage - moving compressed pages between the RAM zstore and the swap
 * device (RECLAIM_DESIGN.md, phases C2 write-back and C3 read-in).
 *
 * Phase C2 (write-back): under memory pressure, when the RAM compressed
 * pool grows large, spill cold blobs to the swap device.  Each blob moves
 * from a zstore RAM slot to a 4 KB swap slot; its phys_block handle is
 * flipped RAM -> disk (pb_zref tag) and the RAM slot is freed, so pool
 * pages empty and return to the allocator.  This raises the effective
 * anonymous-memory ceiling from RAM + pool to RAM + pool + disk.
 *
 * Phase C3 (read-in): a page fault on a disk-resident compressed page
 * reads the blob back asynchronously, decompresses it into a fresh frame,
 * and resumes the faulting process -- mirroring mem_file's async fault
 * (SUSPEND now, wake on completion), but driven by swapio's BDEV reply
 * instead of a VFS reply.
 *
 * Both directions share one swapio transport channel (one I/O on the wire
 * at a time).  A small fixed pool of "jobs", each with its own pre-mapped
 * staging frame, lets several requests queue and complete in turn; a
 * dispatcher issues the next queued job when the channel frees.  Staging
 * frames are reserved at swapon (swappage_init), never under pressure.
 */

#include <assert.h>
#include <string.h>
#include <minix/com.h>
#include <minix/ipc.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>

#include "proto.h"
#include "vm.h"
#include "region.h"
#include "glo.h"
#include "sanitycheck.h"

/* Start writing blobs out to disk once the RAM pool holds at least this
 * many pages of compressed blobs.  The caller (alloc.c) only kicks the
 * tick while below the low free-memory watermark, so write-back happens
 * only under actual memory pressure, not merely because the pool is
 * populated. */
#define WB_POOL_HIGH		128	/* pool pages (~512 KB of blobs) */

/* Concurrent swap jobs (read-in + write-back).  Each reserves one staging
 * frame.  Read-in faults beyond this depth degrade to EFAULT (rare; a
 * heavy concurrent swap-in storm), which the fault path turns into a
 * SIGSEGV -- a documented first-cut limit, hardened later. */
#define NJOBS			16
#define JOBS_WB_RESERVE		(NJOBS / 2)	/* keep half for read-in */

struct swapjob {
	int		 inuse;
	int		 inflight;	/* issued on the swapio channel */
	int		 is_read;
	int		 canceled;	/* write-back: page faulted/freed away */
	unsigned long	 slot;		/* swap slot */
	unsigned char	*stage_va;	/* mapped staging frame */
	phys_bytes	 stage_phys;

	/* write-back context */
	struct phys_block *pb;
	void		*ram_handle;

	/* read-in fault context (enough to re-drive and wake the faulter) */
	endpoint_t	 ep;
	vir_bytes	 vaddr;
	phys_bytes	 new_page;	/* destination frame (decompress into) */
	phys_bytes	 new_page_cl;
	vfs_callback_t	 cb;
	int		 statelen;
	char		 state[sizeof(message)];
};

static struct swapjob jobs[NJOBS];
static int jobs_free;			/* count of !inuse jobs */
static int ready;			/* staging frames allocated */

/*
 * Reserve the per-job staging frames.  Called once when a swap device is
 * configured (do_swapon), never on the reclaim/fault path.
 */
int
swappage_init(void)
{
	int i;

	if (ready)
		return OK;

	for (i = 0; i < NJOBS; i++) {
		if (!(jobs[i].stage_va =
		    vm_allocpage(&jobs[i].stage_phys, VMP_SLAB))) {
			printf("VM: swappage: no staging frame\n");
			return ENOMEM;
		}
	}
	jobs_free = NJOBS;
	ready = 1;
	return OK;
}

static struct swapjob *
job_alloc(void)
{
	int i;

	for (i = 0; i < NJOBS; i++)
		if (!jobs[i].inuse) {
			struct swapjob *j = &jobs[i];
			unsigned char *sv = j->stage_va;
			phys_bytes sp = j->stage_phys;
			memset(j, 0, sizeof(*j));
			j->stage_va = sv;
			j->stage_phys = sp;
			j->inuse = 1;
			jobs_free--;
			return j;
		}
	return NULL;
}

static void
job_free(struct swapjob *j)
{
	j->inuse = 0;
	j->inflight = 0;
	jobs_free++;
}

static void swap_job_done(int status, void *arg);
static void swapin_complete(struct swapjob *j, int status);
static void swapout_complete(struct swapjob *j, int status);

/*
 * Issue the next queued job onto the swapio channel, if it is free.  One
 * I/O on the wire at a time; read-in is preferred over write-back so a
 * blocked faulter is served ahead of background spilling.
 */
static void
swap_dispatch(void)
{
	struct swapjob *j = NULL;
	int i;

	if (!ready || !swapio_configured() || swapio_busy())
		return;

	/* Prefer a queued read-in. */
	for (i = 0; i < NJOBS; i++)
		if (jobs[i].inuse && !jobs[i].inflight && jobs[i].is_read) {
			j = &jobs[i];
			break;
		}
	if (j == NULL)
		for (i = 0; i < NJOBS; i++)
			if (jobs[i].inuse && !jobs[i].inflight) {
				j = &jobs[i];
				break;
			}
	if (j == NULL)
		return;

	/* A write-back canceled before it ever went on the wire: drop it. */
	if (!j->is_read && j->canceled) {
		swapstore_slot_free(j->slot);
		job_free(j);
		swap_dispatch();
		return;
	}

	j->inflight = 1;
	if (j->is_read) {
		if (swapio_read_page(j->slot, j->stage_phys, swap_job_done,
		    j) != OK) {
			/* Could not issue (rare): run the completion path with
			 * a failure so the suspended faulter is still woken
			 * (into a SIGSEGV) rather than hanging forever. */
			swapin_complete(j, EIO);
			job_free(j);
			swap_dispatch();
		}
	} else {
		if (swapio_write_page(j->slot, j->stage_phys, swap_job_done,
		    j) != OK) {
			swapout_complete(j, EIO);	/* leaves blob in RAM */
			job_free(j);
			swap_dispatch();
		}
	}
}

/*===========================================================================*
 *				write-back (C2)				     *
 *===========================================================================*/

/*
 * Look for a cold RAM blob to spill and, if the pool is above the
 * write-back watermark and the channel has capacity, queue it.  Cheap and
 * idempotent: safe to call often (main loop, completions, reclaim path).
 */
void
swapout_tick(void)
{
	struct phys_block *pb;
	struct swapjob *j;
	unsigned long pages, cap, slot;

	if (!ready || !swapio_configured())
		return;
	if (jobs_free <= NJOBS - JOBS_WB_RESERVE)
		return;			/* keep capacity for read-in */

	zstore_pool_usage(&pages, &cap);
	if (pages < WB_POOL_HIGH)
		return;

	if (!(pb = map_find_compressed_ram_page()))
		return;
	if ((slot = swapstore_slot_alloc()) == NO_SWAP_SLOT)
		return;			/* swap device full */

	if (!(j = job_alloc())) {
		swapstore_slot_free(slot);
		return;
	}

	/* Stage the raw blob (header + compressed payload) into the job's
	 * page-sized frame, zero-padded, then queue the write. */
	zstore_blob_copyout(pb->pb_zref, j->stage_va, VM_PAGE_SIZE);
	j->is_read = 0;
	j->slot = slot;
	j->pb = pb;
	j->ram_handle = pb->pb_zref;

	swap_dispatch();
}

/*
 * Is a (live, not-yet-canceled) write-back already outstanding for 'pb'?
 * map_find_compressed_ram_page() consults this so the same RAM blob is
 * never queued for write-back twice (which would double-free its slot).
 */
int
swapout_pb_busy(struct phys_block *pb)
{
	int i;

	for (i = 0; i < NJOBS; i++)
		if (jobs[i].inuse && !jobs[i].is_read && !jobs[i].canceled &&
		    jobs[i].pb == pb)
			return 1;
	return 0;
}

/*
 * A fault or free consumed the RAM blob of 'pb' while its write-back was
 * outstanding: cancel that write-back so completion does not free the
 * (now reused) RAM slot or flip an already-changed handle.
 */
void
swapout_cancel_pb(struct phys_block *pb)
{
	int i;

	for (i = 0; i < NJOBS; i++)
		if (jobs[i].inuse && !jobs[i].is_read && !jobs[i].canceled &&
		    jobs[i].pb == pb)
			jobs[i].canceled = 1;
}

static void
swapout_complete(struct swapjob *j, int status)
{
	if (j->canceled) {
		/* The page was faulted in or freed while we wrote it out; the
		 * RAM slot was already handled by that path.  Just release the
		 * swap slot we reserved. */
		swapstore_slot_free(j->slot);
		return;
	}
	if (status != (int)VM_PAGE_SIZE) {
		/* Write failed: leave the blob in RAM, release the slot. */
		swapstore_slot_free(j->slot);
		return;
	}
	/* Success: free the RAM blob and flip the handle to disk.  The
	 * phys_block stays PBF_COMPRESSED; its contents now live on disk. */
	zstore_free(j->ram_handle);
	USE(j->pb, j->pb->pb_zref = swapstore_slot_to_handle(j->slot););
}

/*===========================================================================*
 *				read-in (C3)				     *
 *===========================================================================*/

/*
 * Start an asynchronous read-in of a disk-resident compressed page.  The
 * fault is suspended (SUSPEND) and resumed from swap_job_done when the
 * blob has been read and decompressed.  'new_page'/'new_page_cl' is the
 * destination frame the caller already allocated.
 *
 * Only carries (ep, vaddr) + the saved fault callback/state across the
 * async gap; the phys_block is re-looked-up on completion (and the read
 * dropped if the process died), so a concurrent exit/unmap cannot leave a
 * dangling pointer -- exactly like mem_file's VFS round-trip.
 */
int
anon_swapin_start(struct vmproc *vmp, struct vir_region *region,
	struct phys_region *ph, int write, vfs_callback_t cb, void *state,
	int len, int *io, phys_bytes new_page, phys_bytes new_page_cl)
{
	struct swapjob *j;
	unsigned long slot;

	assert(cb != NULL);		/* caller checked; retry path never here */
	assert((size_t)len <= sizeof(j->state));

	slot = swapstore_handle_to_slot(ph->ph->pb_zref);

	if (!(j = job_alloc())) {
		/* queue full (rare heavy swap-in storm): free the destination
		 * frame the caller allocated; the fault path turns the error
		 * into a SIGSEGV. */
		free_mem(new_page_cl, 1);
		return ENOMEM;
	}

	j->is_read = 1;
	j->slot = slot;
	j->new_page = new_page;
	j->new_page_cl = new_page_cl;
	j->ep = vmp->vm_endpoint;
	j->vaddr = region->vaddr + ph->offset;
	j->cb = cb;
	j->statelen = len;
	memcpy(j->state, state, len);

	*io = 1;			/* major (disk) fault, for stats */

	swap_dispatch();
	return SUSPEND;
}

/*
 * Commit a just-read-in page: re-look-up the faulting phys_block by
 * (ep, vaddr) and, if it is still the same disk-resident compressed block,
 * install the decompressed frame and free the swap slot.  Returns 1 if the
 * frame was committed (caller keeps it), 0 otherwise (caller frees it).
 */
static int
swapin_commit(struct swapjob *j)
{
	struct vmproc *vmp;
	struct vir_region *region;
	struct phys_region *pr = NULL;
	struct phys_block *pb;
	int p;

	if (vm_isokendpt(j->ep, &p) != OK)
		return 0;		/* process gone */
	vmp = &vmproc[p];
	if (!(vmp->vm_flags & VMF_INUSE) || (vmp->vm_flags & VMF_EXITING))
		return 0;

	if (!(region = map_lookup(vmp, j->vaddr, &pr)) || pr == NULL)
		return 0;
	pb = pr->ph;

	/* Still the same on-disk compressed block for our slot? */
	if (pb->phys != MAP_NONE || !(pb->flags & PBF_COMPRESSED))
		return 0;
	if (!swapstore_handle_is_disk(pb->pb_zref))
		return 0;
	if (swapstore_handle_to_slot(pb->pb_zref) != j->slot)
		return 0;

	/* Decompress the staged blob into the destination frame and install
	 * it.  A shared (post-fork) page becomes resident for all sharers;
	 * a write fault re-COWs on the resume retry (mem_anon handles it). */
	zstore_decompress_buf(j->stage_va, j->new_page);
	USE(pb,
		pb->flags &= ~PBF_COMPRESSED;
		pb->pb_zref = NULL;
		pb->phys = j->new_page;);

	swapstore_slot_free(j->slot);
	return 1;
}

static void
swapin_complete(struct swapjob *j, int status)
{
	message m;
	endpoint_t ep = j->ep;
	vfs_callback_t cb = j->cb;
	char statebuf[sizeof(j->state)];
	int p;

	memcpy(statebuf, j->state, j->statelen);

	{
		int committed = (status == (int)VM_PAGE_SIZE) &&
		    swapin_commit(j);
		if (!committed) {
			/* Read failed, or the page was already resolved / the
			 * process changed underneath us: drop our frame.  The
			 * swap slot is owned by whoever now holds the block (or
			 * was freed on teardown); we only free it if we
			 * committed. */
			free_mem(ABS2CLICK(j->new_page), 1);
		}
	}

	/* Resume the faulter via the saved callback (pf_cont / the kernel
	 * memory-request continuation).  It re-drives the fault; the page is
	 * now resident, so the retry returns OK and wakes the process.  If
	 * the process is gone, the callback drops it harmlessly. */
	memset(&m, 0, sizeof(m));
	if (cb != NULL && vm_isokendpt(ep, &p) == OK)
		cb(&vmproc[p], &m, NULL, statebuf);
}

/*===========================================================================*
 *				completion dispatch			     *
 *===========================================================================*/

static void
swap_job_done(int status, void *arg)
{
	struct swapjob *j = arg;

	if (j->is_read)
		swapin_complete(j, status);
	else
		swapout_complete(j, status);

	job_free(j);

	swap_dispatch();	/* issue the next queued job */
	swapout_tick();		/* and top up write-back if still pressured */
}
