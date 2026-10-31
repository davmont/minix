/* Test 161 - pthread_detach(3) of a thread created joinable: _lwp_detach(2).
 *
 * A thread detached after it was created can no longer be joined, and frees
 * its process slot as it exits; one that has exited already, unjoined, is
 * freed as it is detached.  A thread joining it when it is detached is told
 * there is no such thread.
 */
#include <sys/types.h>
#include <errno.h>
#include <lwp.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

#define ROUNDS	600	/* more than there are process slots */

static volatile int go;
static volatile lwpid_t lid;

static void
sleep_ms(int ms)
{
	struct timespec ts;

	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (ms % 1000) * 1000000L;
	while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
		;
}

/* Wait for 'go', for at most 'arg' ms, then exit. */
static void *
waiter(void *arg)
{
	int ms;

	lid = _lwp_self();
	for (ms = 0; !go && ms < (int)(long) arg; ms++)
		sleep_ms(1);
	return arg;
}

static void *
quick(void *arg)
{
	lid = _lwp_self();
	return arg;
}

static void
test161a(void)
{
	pthread_t t;
	void *res;
	int r;

	subtest = 1;

	/* Joinable, as before. */
	if (pthread_create(&t, NULL, quick, (void *) 7) != 0) e(1);
	if (pthread_join(t, &res) != 0 || res != (void *) 7) e(2);

	/* Detached while it runs: no join, though it is still there.  (The
	 * stub let the join wait for it, and succeed.)
	 */
	go = 0;
	if (pthread_create(&t, NULL, waiter, (void *) 200) != 0) e(3);
	if (pthread_detach(t) != 0) e(4);
	if ((r = pthread_join(t, NULL)) != EINVAL) e(5);	/* not joinable */
	go = 1;
	sleep_ms(50);

	/* Detached once it has exited. */
	if (pthread_create(&t, NULL, quick, NULL) != 0) e(6);
	sleep_ms(50);
	if (pthread_detach(t) != 0) e(7);
}

static void
test161b(void)
{
	pthread_t t;
	int i;

	subtest = 2;

	/* Neither leaves its slot behind: the stub had them all wait for a
	 * join, and pthread_create() ran out of slots.
	 */
	for (i = 0; i < ROUNDS; i++) {
		go = (i & 1);
		if (pthread_create(&t, NULL, (i & 1) ? quick : waiter,
		    (void *) 1000) != 0) {
			e(1);
			break;
		}
		if (i & 1)
			sleep_ms(2);
		if (pthread_detach(t) != 0) e(2);
		go = 1;
	}
	sleep_ms(100);
	if (pthread_create(&t, NULL, quick, NULL) != 0) e(3);
	if (pthread_join(t, NULL) != 0) e(4);
}

static pthread_t target;
static volatile int joined = -1;

static void *
joiner(void *arg)
{
	joined = pthread_join(target, NULL);
	return arg;
}

static void
test161c(void)
{
	pthread_t j;
	lwpid_t l;

	subtest = 3;

	/* _lwp_detach(2) itself: no such thread, and once only. */
	if (_lwp_detach(999999) != -1 || errno != ESRCH) e(1);
	go = 0;
	lid = 0;
	if (pthread_create(&target, NULL, waiter, (void *) 2000) != 0) e(2);
	while (lid == 0)
		sleep_ms(1);
	l = lid;

	/* One joining it as it is detached is let go. */
	if (pthread_create(&j, NULL, joiner, NULL) != 0) e(3);
	sleep_ms(100);
	if (joined != -1) e(4);
	if (_lwp_detach(l) != 0) e(5);
	if (pthread_join(j, NULL) != 0) e(6);
	if (joined != ESRCH) e(7);
	if (_lwp_detach(l) != -1 || errno != EINVAL) e(8);
	go = 1;
	sleep_ms(50);
	if (_lwp_detach(l) != -1 || errno != ESRCH) e(9);	/* gone */

	/* One that has exited, once only too. */
	lid = 0;
	if (pthread_create(&j, NULL, quick, NULL) != 0) e(10);
	sleep_ms(50);
	l = lid;
	if (_lwp_detach(l) != 0) e(11);
	if (_lwp_detach(l) != -1 || errno != ESRCH) e(12);
	if (_lwp_wait(l, NULL) != -1 || errno != ESRCH) e(13);
}

int
main(int argc, char **argv)
{
	start(161);

	alarm(120);
	test161a();
	test161b();
	test161c();
	alarm(0);

	quit();
	return 0;
}
