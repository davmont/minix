/* Multithreading glue for the block cache.
 *
 * A multithreaded file system -- one that runs several worker threads on top of
 * libmthread, see libfsdriver's fsdriver_mt_task() -- cannot use the cache's
 * ordinary synchronous bdev calls: a synchronous bdev_read() blocks the whole
 * server process in the kernel until the disk driver replies, which would stall
 * every other worker.  This file provides an alternative disk-transfer hook
 * that issues the request asynchronously and then suspends only the calling
 * worker thread (through an mthread event) until the reply arrives.  The
 * server's main thread receives the reply and hands it to lmfs_mt_reply(),
 * which fires the event and wakes the worker.
 *
 * This is kept in a separate translation unit so that file systems which do not
 * enable multithreading do not pull in the mthread dependency.
 */

#define _SYSTEM

#include <assert.h>
#include <errno.h>
#include <string.h>

#include <minix/drivers.h>
#include <minix/libminixfs.h>
#include <minix/bdev.h>
#include <minix/mthread.h>
#include <minix/sysutil.h>

#include "inc.h"

/* Per-transfer state shared between the worker thread that issues a disk
 * request and the main thread that receives the reply for it. */
struct mt_io {
	mthread_event_t event;	/* worker waits on this; the reply fires it */
	int done;		/* set by the callback when the reply arrives */
	ssize_t result;		/* transfer result (bytes or negative errno) */
};

/*
 * Callback invoked from the main thread (via lmfs_mt_reply -> bdev_reply_asyn)
 * when the disk driver's reply for an outstanding request comes in.  Record the
 * result and wake the worker thread that is waiting for it.
 */
static void mt_io_callback(dev_t __unused dev, bdev_id_t __unused id,
	bdev_param_t param, int result)
{
	struct mt_io *io = (struct mt_io *) param;

	io->result = result;
	io->done = TRUE;
	mthread_event_fire(&io->event);
}

/*
 * Disk-transfer hook installed in the block cache for multithreaded file
 * systems.  Issue an asynchronous bdev request matching the synchronous call
 * the cache would otherwise make, then suspend the calling worker thread until
 * the reply wakes it.  Returns what the corresponding synchronous bdev call
 * would return.
 */
static ssize_t mt_io_hook(int req, dev_t dev, u64_t pos, char *buf,
	iovec_t *vec, int cnt, size_t count)
{
	struct mt_io io;
	bdev_id_t id;

	if (mthread_event_init(&io.event) != 0)
		panic("libminixfs: cannot initialize mt I/O event");
	io.done = FALSE;
	io.result = EIO;

	switch (req) {
	case BDEV_READ:
		id = bdev_read_asyn(dev, pos, buf, count, BDEV_NOFLAGS,
		    mt_io_callback, (bdev_param_t) &io);
		break;
	case BDEV_GATHER:
		id = bdev_gather_asyn(dev, pos, vec, cnt, BDEV_NOFLAGS,
		    mt_io_callback, (bdev_param_t) &io);
		break;
	default:
		panic("libminixfs: bad mt I/O request %d", req);
	}

	/* If the request could not even be sent, there is nothing to wait for. */
	if (id < 0) {
		mthread_event_destroy(&io.event);
		return (ssize_t) id;	/* negative error code */
	}

	/* Suspend this worker until the main thread routes the reply to us.  In
	 * the cooperative mthread model the reply cannot be processed until we
	 * yield here, so there is no lost-wakeup race; the loop is defensive.
	 */
	while (!io.done)
		mthread_event_wait(&io.event);

	mthread_event_destroy(&io.event);

	return io.result;
}

/*
 * Enable multithreaded operation of the block cache.  Called once at startup by
 * a file system that uses worker threads.
 */
void lmfs_enable_mt(void)
{
	lmfs_set_io_hook(mt_io_hook);
}

/*
 * Hand a reply received from a disk driver to the asynchronous bdev layer,
 * which matches it to the outstanding request and wakes the worker thread that
 * is waiting for it.  Called from the file system's main thread.
 */
void lmfs_mt_reply(message *m)
{
	bdev_reply_asyn(m);
}
