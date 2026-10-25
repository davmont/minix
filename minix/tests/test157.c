/* Test 157 - pthread_cancel() of a thread blocked at a cancellation point.
 *
 * pthread_cancel() wakes its target with _lwp_wakeup(), which on MINIX did
 * nothing (ENOSYS): a thread already blocked in pthread_join() or
 * pthread_cond_wait() was never cancelled, and whoever joined it waited
 * forever.  Here each kind of thread is cancelled while blocked, and, many
 * times over, while about to block.  An alarm ends the test if a cancel is
 * lost.  (System calls such as sigwait() or waitpid() are not cancellation
 * points on MINIX yet: libpthread is built without its cancellation stubs.)
 */
#include <sys/types.h>
#include <pthread.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

#define ROUNDS	50

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cnd = PTHREAD_COND_INITIALIZER;
static pthread_t target;

static void *
locker(void *arg)
{
	pthread_mutex_lock(&mtx);
	pthread_mutex_unlock(&mtx);
	return arg;
}

static void *
joiner(void *arg)
{
	pthread_join(target, NULL);
	return arg;			/* not cancelled */
}

static void
unlock(void *arg)
{
	pthread_mutex_unlock(&mtx);
}

static void *
cond_waiter(void *arg)
{
	pthread_mutex_lock(&mtx);
	pthread_cleanup_push(unlock, NULL);
	for (;;)
		pthread_cond_wait(&cnd, &mtx);
	pthread_cleanup_pop(1);
	return arg;
}

/* Start 'func', let it block (or not, with 'settle' 0), cancel it and check
 * that it was: error 'n' if it was not, or could not be set up.
 */
static void
cancel_one(void *(*func)(void *), int settle, int n)
{
	pthread_t t;
	void *res;

	if (pthread_create(&t, NULL, func, (void *)1L) != 0) e(n);
	if (settle)
		usleep(20000);
	if (pthread_cancel(t) != 0) e(n);
	if (pthread_join(t, &res) != 0 || res != PTHREAD_CANCELED) e(n);
}

static void
test157a(void)
{
	void *res;
	int i;

	subtest = 1;

	/* pthread_join/4-1: the joined thread stays joinable. */
	for (i = 0; i < ROUNDS; i++) {
		pthread_mutex_lock(&mtx);
		if (pthread_create(&target, NULL, locker, NULL) != 0) e(1);
		cancel_one(joiner, i & 1, 2);
		pthread_mutex_unlock(&mtx);
		if (pthread_join(target, &res) != 0 || res != NULL) e(3);
	}
}

static void
test157b(void)
{
	int i;

	subtest = 2;

	for (i = 0; i < ROUNDS; i++) {
		cancel_one(cond_waiter, i & 1, 1);
		/* The cleanup handler gave the mutex back. */
		if (pthread_mutex_trylock(&mtx) != 0) e(2);
		pthread_mutex_unlock(&mtx);
	}
}

int
main(int argc, char **argv)
{
	start(157);

	alarm(120);			/* a lost cancel hangs: end the test */
	test157a();
	test157b();
	alarm(0);

	quit();
	return 0;
}
