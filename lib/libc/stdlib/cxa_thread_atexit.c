/*
 * __cxa_thread_atexit - run destructors for thread_local objects.
 *
 * The C++ ABI calls this to register a destructor for an object with
 * thread-storage duration.  libc++ emits a call to it for every thread_local
 * with a non-trivial destructor, so without it nothing that uses one can be
 * linked -- which is to say, most of Qt.
 *
 * The destructors are held on a per-thread list and run when the thread exits.
 * That is what the libc-internal thread API is for: its weak stubs work in a
 * program that never links libpthread, and libpthread overrides them in one
 * that does, so this is correct either way.
 *
 * A destructor registered on the main thread would otherwise never run at all
 * -- the main thread does not "exit" through the thread machinery, it returns
 * from main() -- so those are drained from an atexit() handler as well.
 */

#include <sys/cdefs.h>

/*
 * reentrant.h only declares the thread key API (and __isthreaded) under
 * _REENTRANT.  We want it: the weak stubs make it safe in a program that never
 * links libpthread, and libpthread overrides them in one that does.
 */
#define _REENTRANT

#include "namespace.h"

#include <stdlib.h>

#include "reentrant.h"

struct cxa_thread_dtor {
	void			(*func)(void *);
	void			 *obj;
	void			 *dso;
	struct cxa_thread_dtor	 *next;
};

static thread_key_t	cxa_thread_key;
static once_t		cxa_thread_once = ONCE_INITIALIZER;

/* The main thread's list, drained by atexit(); see below. */
static struct cxa_thread_dtor *cxa_main_dtors;
static int cxa_main_atexit_done;

static void
cxa_run_dtors(void *arg)
{
	struct cxa_thread_dtor *d = arg, *next;

	/*
	 * Most recently registered first, as the ABI requires: a thread_local
	 * destroyed later must not depend on one destroyed earlier.
	 */
	while (d != NULL) {
		next = d->next;
		(*d->func)(d->obj);
		free(d);
		d = next;
	}
}

static void
cxa_run_main_dtors(void)
{
	struct cxa_thread_dtor *d = cxa_main_dtors;

	cxa_main_dtors = NULL;
	cxa_run_dtors(d);
}

static void
cxa_thread_key_init(void)
{
	(void)thr_keycreate(&cxa_thread_key, cxa_run_dtors);
}

int
__cxa_thread_atexit(void (*func)(void *), void *obj, void *dso)
{
	struct cxa_thread_dtor *d;

	if (func == NULL)
		return -1;

	thr_once(&cxa_thread_once, cxa_thread_key_init);

	if ((d = malloc(sizeof(*d))) == NULL)
		return -1;

	d->func = func;
	d->obj = obj;
	d->dso = dso;

	/*
	 * In a single-threaded program the key stubs keep no per-thread value
	 * and no thread ever exits to run the key destructor, so the list would
	 * be dropped on the floor.  Keep those on a list of our own and drain it
	 * from atexit(), which is when a main-thread thread_local is supposed to
	 * be destroyed anyway.
	 */
	if (thr_getspecific(cxa_thread_key) == NULL && !__isthreaded) {
		d->next = cxa_main_dtors;
		cxa_main_dtors = d;
		if (!cxa_main_atexit_done) {
			cxa_main_atexit_done = 1;
			(void)atexit(cxa_run_main_dtors);
		}
		return 0;
	}

	d->next = thr_getspecific(cxa_thread_key);
	if (thr_setspecific(cxa_thread_key, d) != 0) {
		free(d);
		return -1;
	}

	return 0;
}

/*
 * glibc spells the same thing __cxa_thread_atexit_impl, and some C++ runtimes
 * look for that name instead.  Give them both.
 */
int
__cxa_thread_atexit_impl(void (*func)(void *), void *obj, void *dso)
{
	return __cxa_thread_atexit(func, obj, dso);
}
