/* Multithreaded variant of the file-system driver message loop.
 *
 * The ordinary fsdriver_task() loop (fsdriver.c) handles one request at a time:
 * it receives a request from VFS, runs the matching callback to completion --
 * blocking the whole server process while the callback waits for the disk --
 * and only then receives the next request.  fsdriver_mt_task() instead runs a
 * pool of cooperative worker threads (libmthread).  The main thread receives
 * messages and hands each VFS request to a worker; a worker that blocks on disk
 * I/O (through the asynchronous block-cache path, see libminixfs/cache_mt.c)
 * yields to the others, and the main thread routes the disk driver's replies
 * back to the waiting workers via the file system's 'other' callback.
 *
 * This first phase keeps a single global lock around each request so that the
 * file system code still sees strictly serialized execution: it establishes the
 * threading machinery (worker pool, asynchronous I/O, reply demultiplexing)
 * without yet relying on any finer-grained locking inside the file system.
 * Later phases narrow the lock to allow requests to overlap their disk I/O.
 */

#include "fsdriver.h"
#include <minix/mthread.h>
#include <assert.h>

/* The pool need only be as large as the maximum number of requests VFS can have
 * outstanding to a single file system at once (its worker-thread count), plus a
 * small margin. */
#define MT_MAX_WORKERS	12
#define MT_QUEUE_SIZE	32

static const struct fsdriver *mt_fdp;
static unsigned int mt_nworkers;

static mthread_thread_t mt_thread[MT_MAX_WORKERS];
static mthread_event_t mt_work_event;	/* idle workers wait here for work */
static mthread_mutex_t mt_fs_lock;	/* global file-system lock (phase 1) */

/* Simple FIFO ring of pending requests, filled by the main thread and drained
 * by the workers. */
static struct {
	message m;
	int ipc_status;
} mt_queue[MT_QUEUE_SIZE];
static unsigned int mt_q_head, mt_q_tail, mt_q_count;

/* Per-worker flag: is the request this worker is currently handling read-only
 * (it modifies no file-system metadata and starts no journal transaction)?
 * Stored in thread-specific data so it survives the I/O yields, and queried by
 * the block cache (fsdriver_mt_readonly) to decide whether it may also drop the
 * global lock around the request's *metadata* reads -- safe only because such a
 * request neither writes metadata nor allocates. */
static mthread_key_t mt_readonly_key;

/*
 * Return whether the given VFS request only reads file-system state.  These
 * requests perform no metadata write and start no journal transaction, so the
 * cache may overlap their metadata I/O with other work.  Conservative: anything
 * not listed here is treated as a writer and stays fully serialized.
 */
static int mt_request_is_readonly(const message *m_ptr)
{
	switch (TRNS_DEL_ID(m_ptr->m_type)) {
	case REQ_STAT:
	case REQ_STATVFS:
	case REQ_GETDENTS:
	case REQ_RDLINK:
	case REQ_READ:		/* only updates atime, after all its reads */
	case REQ_PEEK:
		/* These all operate on an inode that already has a VFS vnode, so
		 * it is pinned in the in-core inode table and no cold inode load
		 * happens while the lock is dropped.  REQ_LOOKUP is deliberately
		 * excluded: it loads cold inodes during path resolution, and the
		 * in-core inode table does not yet serialize concurrent loads of
		 * the same inode (get_inode() hashes an inode only after reading
		 * it), so two lookups could otherwise build duplicate in-core
		 * copies.  Lifting that needs an inode-load guard -- future work.
		 */
		return TRUE;
	default:
		return FALSE;
	}
}

/*
 * Report whether the calling worker thread is currently serving a read-only
 * request.  Returns FALSE on the main thread or before any request is assigned.
 */
int fsdriver_mt_readonly(void)
{
	return (mthread_getspecific(mt_readonly_key) != NULL);
}

/*
 * Remove the oldest pending request from the queue.  Returns TRUE if one was
 * dequeued, FALSE if the queue was empty.
 */
static int mt_dequeue(message *m, int *ipc_status)
{
	if (mt_q_count == 0)
		return FALSE;

	*m = mt_queue[mt_q_head].m;
	*ipc_status = mt_queue[mt_q_head].ipc_status;
	mt_q_head = (mt_q_head + 1) % MT_QUEUE_SIZE;
	mt_q_count--;

	return TRUE;
}

/*
 * Append a request to the queue and wake one idle worker.
 */
static void mt_enqueue(const message *m, int ipc_status)
{
	if (mt_q_count == MT_QUEUE_SIZE)
		panic("fsdriver_mt: request queue overflow");

	mt_queue[mt_q_tail].m = *m;
	mt_queue[mt_q_tail].ipc_status = ipc_status;
	mt_q_tail = (mt_q_tail + 1) % MT_QUEUE_SIZE;
	mt_q_count++;

	mthread_event_fire(&mt_work_event);
}

/*
 * Worker thread main loop.  Repeatedly take a request off the queue and process
 * it under the global file-system lock, sending the reply to VFS
 * asynchronously.  Always try the queue before blocking, so that a request
 * enqueued while every worker was busy (its wakeup signal then lost) is still
 * picked up by the first worker to come free.
 */
static void *mt_worker(void *arg __unused)
{
	message m;
	int ipc_status;

	while (fsdriver_running || fsdriver_mounted) {
		if (!mt_dequeue(&m, &ipc_status)) {
			mthread_event_wait(&mt_work_event);
			continue;
		}

		/* Record whether this is a read-only request, for the cache. */
		(void)mthread_setspecific(mt_readonly_key,
		    mt_request_is_readonly(&m) ? (void *) 1 : NULL);

		mthread_mutex_lock(&mt_fs_lock);

		fsdriver_process(mt_fdp, &m, ipc_status, TRUE /*asyn_reply*/);

		mthread_mutex_unlock(&mt_fs_lock);

		(void)mthread_setspecific(mt_readonly_key, NULL);
	}

	return NULL;
}

/*
 * Release and re-acquire the global file-system lock.  Exposed so that the
 * block cache can drop the lock around a data-block transfer (letting other
 * workers run) and take it again afterwards; see lmfs_enable_mt().  Only ever
 * called by a worker thread that currently holds the lock.
 */
void fsdriver_mt_unlock(void)
{
	mthread_mutex_unlock(&mt_fs_lock);
}

void fsdriver_mt_lock(void)
{
	mthread_mutex_lock(&mt_fs_lock);
}

/*
 * Handle one message received by the main thread.  Requests from VFS are queued
 * for a worker thread; everything else -- notifications and, in particular, the
 * asynchronous replies from the disk driver -- is passed to the file system's
 * 'other' callback, which is expected to route disk replies to lmfs_mt_reply().
 */
static void mt_handle_message(message *m, int ipc_status)
{
	if (is_ipc_notify(ipc_status) || m->m_source != VFS_PROC_NR) {
		if (mt_fdp->fdr_other != NULL)
			mt_fdp->fdr_other(m, ipc_status);

		return;
	}

	mt_enqueue(m, ipc_status);
}

/*
 * Multithreaded main program of a file-system service.  Equivalent to
 * fsdriver_task(), but dispatches requests across 'nworkers' cooperative worker
 * threads.  Returns when the file system has been terminated and unmounted.
 */
void fsdriver_mt_task(struct fsdriver *fdp, unsigned int nworkers)
{
	message m;
	int r, ipc_status;
	unsigned int i;

	mt_fdp = fdp;

	if (nworkers < 1)
		nworkers = 1;
	if (nworkers > MT_MAX_WORKERS)
		nworkers = MT_MAX_WORKERS;
	mt_nworkers = nworkers;

	mt_q_head = mt_q_tail = mt_q_count = 0;

	if (mthread_event_init(&mt_work_event) != 0)
		panic("fsdriver_mt: cannot initialize work event");
	if (mthread_mutex_init(&mt_fs_lock, NULL) != 0)
		panic("fsdriver_mt: cannot initialize file-system lock");
	if (mthread_key_create(&mt_readonly_key, NULL) != 0)
		panic("fsdriver_mt: cannot create read-only key");

	fsdriver_running = TRUE;

	for (i = 0; i < mt_nworkers; i++)
		if (mthread_create(&mt_thread[i], NULL, mt_worker, NULL) != 0)
			panic("fsdriver_mt: cannot create worker thread %u", i);

	while (fsdriver_running || fsdriver_mounted) {
		if ((r = sef_receive_status(ANY, &m, &ipc_status)) != OK) {
			if (r == EINTR)
				continue;	/* sef_cancel() was called */

			panic("fsdriver_mt: sef_receive_status failed: %d", r);
		}

		mt_handle_message(&m, ipc_status);

		/* Let the worker threads run until they all block again. */
		mthread_yield_all();
	}

	/* The loop only ends once the file system is both terminated and
	 * unmounted, i.e. quiescent with no requests in flight, so the workers
	 * are all idle (blocked waiting for work).  Wake them so they observe
	 * the stopped state and exit, then flush any remaining dirty state.
	 */
	mthread_event_fire_all(&mt_work_event);
	mthread_yield_all();

	if (mt_fdp->fdr_sync != NULL)
		mt_fdp->fdr_sync();
}
