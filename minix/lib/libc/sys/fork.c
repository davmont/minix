#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <sys/queue.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __weak_alias
__weak_alias(pthread_atfork, _pthread_atfork)
__weak_alias(fork, _fork)
#endif

/*
 * pthread_atfork(3) support.
 *
 * MINIX's fork() is a message call to PM (not the NetBSD __fork() path), so the
 * generic lib/libc/gen/pthread_atfork.c is not built here.  Without it a
 * multithreaded process that fork()s runs none of the prepare/parent/child
 * handlers that libraries register to keep their locks and helper threads
 * consistent across the fork -- e.g. libpthread (its internal locks and
 * thread-specific data), and, through them, glib/Qt clients.  On MINIX that
 * manifests as a Wayland client (the LXQt panel) wedging its event dispatch
 * after it launches a program with QProcess.  Implement the handlers here,
 * around the PM fork message.
 *
 * MINIX libc is built without _REENTRANT, so the reentrant.h mutex types are
 * not available and are not used to guard the handler lists.  That is safe in
 * practice: handlers are registered from library initialisers (before the
 * process spawns threads or forks), so the lists are stable by the time any
 * fork() runs, and the handlers themselves perform their own locking.
 */
struct atfork_callback {
	SIMPLEQ_ENTRY(atfork_callback) next;
	void (*fn)(void);
};
SIMPLEQ_HEAD(atfork_callback_q, atfork_callback);

static struct atfork_callback_q prepareq = SIMPLEQ_HEAD_INITIALIZER(prepareq);
static struct atfork_callback_q parentq = SIMPLEQ_HEAD_INITIALIZER(parentq);
static struct atfork_callback_q childq = SIMPLEQ_HEAD_INITIALIZER(childq);

int
pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void))
{
	struct atfork_callback *newprepare = NULL;
	struct atfork_callback *newparent = NULL;
	struct atfork_callback *newchild = NULL;

	if (prepare != NULL) {
		if ((newprepare = malloc(sizeof(*newprepare))) == NULL)
			return ENOMEM;
		newprepare->fn = prepare;
	}
	if (parent != NULL) {
		if ((newparent = malloc(sizeof(*newparent))) == NULL) {
			free(newprepare);
			return ENOMEM;
		}
		newparent->fn = parent;
	}
	if (child != NULL) {
		if ((newchild = malloc(sizeof(*newchild))) == NULL) {
			free(newprepare);
			free(newparent);
			return ENOMEM;
		}
		newchild->fn = child;
	}

	/* prepare handlers run LIFO, parent/child handlers FIFO. */
	if (newprepare != NULL)
		SIMPLEQ_INSERT_HEAD(&prepareq, newprepare, next);
	if (newparent != NULL)
		SIMPLEQ_INSERT_TAIL(&parentq, newparent, next);
	if (newchild != NULL)
		SIMPLEQ_INSERT_TAIL(&childq, newchild, next);

	return 0;
}

static pid_t
__minix_raw_fork(void)
{
	message m;

	memset(&m, 0, sizeof(m));
	return (_syscall(PM_PROC_NR, PM_FORK, &m));
}

pid_t fork(void)
{
	struct atfork_callback *iter;
	pid_t ret;

	SIMPLEQ_FOREACH(iter, &prepareq, next)
		(*iter->fn)();

	ret = __minix_raw_fork();

	if (ret != 0) {
		/* Parent (whether fork succeeded or failed). */
		SIMPLEQ_FOREACH(iter, &parentq, next)
			(*iter->fn)();
	} else {
		/* Child. */
		SIMPLEQ_FOREACH(iter, &childq, next)
			(*iter->fn)();
	}

	return ret;
}
