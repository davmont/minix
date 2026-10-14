/* Test 135 - timed waits and signals of parked threads.
 *
 * Threads block in _lwp_park(2), which PM did not time out: a
 * pthread_cond_timedwait() without a wakeup never returned.  Nor did a
 * parked thread run a signal handler before something woke it.
 */
#include <sys/types.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond;
static volatile sig_atomic_t caught;
static int done;

static long
ms_since(const struct timespec *t0)
{
	struct timespec t1;

	clock_gettime(CLOCK_MONOTONIC, &t1);
	return (t1.tv_sec - t0->tv_sec) * 1000 +
	    (t1.tv_nsec - t0->tv_nsec) / 1000000;
}

/* A deadline 'ms' milliseconds from now on the given clock. */
static void
deadline(clockid_t clk, long ms, struct timespec *ts)
{
	clock_gettime(clk, ts);
	ts->tv_sec += ms / 1000;
	ts->tv_nsec += (ms % 1000) * 1000000;
	if (ts->tv_nsec >= 1000000000) {
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000;
	}
}

static void
init_cond(clockid_t clk)
{
	pthread_condattr_t attr;

	if (pthread_condattr_init(&attr) != 0) e(1);
	if (pthread_condattr_setclock(&attr, clk) != 0) e(2);
	if (pthread_cond_init(&cond, &attr) != 0) e(3);
	pthread_condattr_destroy(&attr);
}

/* A wait without a wakeup ends at its deadline, on either clock. */
static void
test_timeout(clockid_t clk)
{
	struct timespec t0, ts;
	long ms;

	subtest = (clk == CLOCK_MONOTONIC) ? 2 : 1;
	init_cond(clk);
	pthread_mutex_lock(&mutex);
	clock_gettime(CLOCK_MONOTONIC, &t0);
	deadline(clk, 200, &ts);
	if (pthread_cond_timedwait(&cond, &mutex, &ts) != ETIMEDOUT) e(4);
	ms = ms_since(&t0);
	if (ms < 190 || ms > 5000) e(5);

	/* A deadline in the past ends the wait at once. */
	deadline(clk, -1000, &ts);
	clock_gettime(CLOCK_MONOTONIC, &t0);
	if (pthread_cond_timedwait(&cond, &mutex, &ts) != ETIMEDOUT) e(6);
	if (ms_since(&t0) > 1000) e(7);

	/* So does an invalid one. */
	ts.tv_nsec = 1000000000;
	if (pthread_cond_timedwait(&cond, &mutex, &ts) != EINVAL) e(8);
	pthread_mutex_unlock(&mutex);
	pthread_cond_destroy(&cond);
}

static void *
waker(void *arg)
{

	usleep(50000);
	pthread_mutex_lock(&mutex);
	done = 1;
	pthread_cond_signal(&cond);
	pthread_mutex_unlock(&mutex);
	return NULL;
}

/* A wait that is woken in time returns 0, and its timer is gone: a later
 * wait is not cut short by it.
 */
static void
test_wakeup(void)
{
	struct timespec t0, ts;
	pthread_t t;
	int r;

	subtest = 3;
	init_cond(CLOCK_MONOTONIC);
	done = 0;
	if (pthread_create(&t, NULL, waker, NULL) != 0) e(1);
	pthread_mutex_lock(&mutex);
	deadline(CLOCK_MONOTONIC, 300, &ts);
	while (!done)
		if ((r = pthread_cond_timedwait(&cond, &mutex, &ts)) != 0) {
			e(2);
			break;
		}
	clock_gettime(CLOCK_MONOTONIC, &t0);
	deadline(CLOCK_MONOTONIC, 1000, &ts);
	if (pthread_cond_timedwait(&cond, &mutex, &ts) != ETIMEDOUT) e(3);
	if (ms_since(&t0) < 990) e(4);
	pthread_mutex_unlock(&mutex);
	pthread_join(t, NULL);
	pthread_cond_destroy(&cond);
}

static void
on_usr1(int sig)
{

	caught = 1;
}

static void *
sleeper(void *arg)
{

	pthread_mutex_lock(&mutex);
	while (!done)
		pthread_cond_wait(&cond, &mutex);
	pthread_mutex_unlock(&mutex);
	return NULL;
}

/* A thread waiting without a timeout runs a signal handler right away. */
static void
test_signal(void)
{
	struct sigaction sa;
	pthread_t t;
	int i;

	subtest = 4;
	init_cond(CLOCK_MONOTONIC);
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_usr1;
	if (sigaction(SIGUSR1, &sa, NULL) != 0) e(1);
	done = 0;
	caught = 0;
	if (pthread_create(&t, NULL, sleeper, NULL) != 0) e(2);
	usleep(100000);			/* let it park */
	if (pthread_kill(t, SIGUSR1) != 0) e(3);
	for (i = 0; i < 100 && !caught; i++)
		usleep(10000);
	if (!caught) e(4);

	pthread_mutex_lock(&mutex);
	done = 1;
	pthread_cond_signal(&cond);
	pthread_mutex_unlock(&mutex);
	pthread_join(t, NULL);
	pthread_cond_destroy(&cond);
}

int
main(int argc, char **argv)
{

	start(135);

	/* Without the fix the first wait never returns. */
	alarm(60);

	test_timeout(CLOCK_REALTIME);
	test_timeout(CLOCK_MONOTONIC);
	test_wakeup();
	test_signal();

	quit();
	return 0;
}
