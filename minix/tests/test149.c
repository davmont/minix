/* Test 149 - timed waits and the realtime clock after the clock is set.
 *
 * The kernel counts CLOCK_REALTIME as a boot time plus a 32-bit tick count.
 * Setting the clock years ahead of the boot time (back again after setting
 * it far into the past, as the Open POSIX Test Suite does) overflowed the
 * count on amd64, where the old bound was LONG_MAX/2: the clock came out
 * more than a year wrong.  And PM took an absolute timeout more than a year
 * past the boot time for one too far away for a timer, so a timed wait such
 * as pthread_cond_timedwait() never timed out.
 *
 * And setting the clock moved CLOCK_MONOTONIC with it.
 *
 * Needs root; the clock is put back as it would have been.
 */
#include <sys/types.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

#define PAST	1037128358	/* November 2002, as the POSIX tests use */

static struct timespec real0, mono0;

/* What the clock would say now had it not been set. */
static void
real_now(struct timespec *ts)
{
	struct timespec now;

	(void)clock_gettime(CLOCK_MONOTONIC, &now);
	ts->tv_sec = real0.tv_sec + (now.tv_sec - mono0.tv_sec);
	ts->tv_nsec = real0.tv_nsec + (now.tv_nsec - mono0.tv_nsec);
	if (ts->tv_nsec < 0) {
		ts->tv_sec--;
		ts->tv_nsec += 1000000000L;
	} else if (ts->tv_nsec >= 1000000000L) {
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000L;
	}
}

static void
restore_clock(void)
{
	struct timespec ts;

	real_now(&ts);
	(void)clock_settime(CLOCK_REALTIME, &ts);
}

static void
set_clock(time_t sec)
{
	struct timespec ts, got;

	ts.tv_sec = sec;
	ts.tv_nsec = 0;
	if (clock_settime(CLOCK_REALTIME, &ts) != 0) e(1);
	if (clock_gettime(CLOCK_REALTIME, &got) != 0) e(2);
	if (got.tv_sec < sec || got.tv_sec > sec + 2) e(3);
}

static void
alarm_handler(int sig)
{

	(void)sig;
	restore_clock();
	e(4);			/* the timed wait did not time out */
	quit();
}

/* A timed wait 2 seconds from now times out, in about that time. */
static void
timed_wait(void)
{
	pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
	struct timespec ts, t0, t1;
	int r;

	alarm(10);
	(void)clock_gettime(CLOCK_MONOTONIC, &t0);
	(void)clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += 2;
	pthread_mutex_lock(&mtx);
	do
		r = pthread_cond_timedwait(&cv, &mtx, &ts);
	while (r == 0);
	pthread_mutex_unlock(&mtx);
	alarm(0);
	(void)clock_gettime(CLOCK_MONOTONIC, &t1);
	if (r != ETIMEDOUT) e(5);
	if (t1.tv_sec - t0.tv_sec < 1 || t1.tv_sec - t0.tv_sec > 4) e(6);
}

/* Into the past and back, as the POSIX clock_settime() tests do. */
static void
test149a(void)
{
	struct timespec ts, m0, m1;

	subtest = 1;
	(void)clock_gettime(CLOCK_MONOTONIC, &m0);
	set_clock(PAST);
	(void)clock_gettime(CLOCK_MONOTONIC, &m1);
	if (m1.tv_sec < m0.tv_sec || m1.tv_sec > m0.tv_sec + 2) e(8);
	timed_wait();
	real_now(&ts);
	set_clock(ts.tv_sec);		/* 24 years ahead of the boot time */
	timed_wait();
	restore_clock();
}

/* The clock about a year past the boot time (INT_MAX ticks, as far as a
 * timer reaches): a timeout 2 seconds later is near, not out of reach.
 */
static void
test149b(void)
{
	long hz;

	subtest = 2;
	hz = sysconf(_SC_CLK_TCK);
	if (hz <= 0) e(7);
	set_clock(PAST);			/* the boot time goes back to it */
	set_clock(PAST + INT_MAX / hz - 1);
	timed_wait();
	restore_clock();
}

int
main(int argc, char **argv)
{

	start(149);

	setuid(geteuid());
	if (getuid() != 0) {
		printf("Test 149 has to be run as root; test aborted\n");
		exit(1);
	}

	(void)clock_gettime(CLOCK_REALTIME, &real0);
	(void)clock_gettime(CLOCK_MONOTONIC, &mono0);
	signal(SIGALRM, alarm_handler);

	test149a();
	test149b();

	restore_clock();
	quit();
	return 0;
}
