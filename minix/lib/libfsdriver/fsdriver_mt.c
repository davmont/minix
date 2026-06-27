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

		mthread_mutex_lock(&mt_fs_lock);

		fsdriver_process(mt_fdp, &m, ipc_status, TRUE /*asyn_reply*/);

		mthread_mutex_unlock(&mt_fs_lock);
	}

	return NULL;
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
