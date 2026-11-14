/* Test 173 - clock_gettime(2) between ticks.
 *
 * CLOCK_REALTIME and CLOCK_MONOTONIC are read off the TSC, to the nanosecond,
 * where the kernel says the TSC may be used: the clocks then move between
 * ticks, never run back (on any CPU, nor while adjtime(2) runs realtime fast
 * or slow), and agree with sleeping and with clock_settime(2).  Where the TSC
 * may not be used, they are the tick clocks, and only that is checked.  The
 * test is setuid root, for adjtime() and clock_settime().
 */
#include <sys/types.h>
#include <sys/time.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

#define NSEC	1000000000LL

static int hires;
static long tick_ns;

static int64_t
now(clockid_t clk)
{
	struct timespec ts;

	if (clock_gettime(clk, &ts) != 0) e(90);
	if (ts.tv_nsec < 0 || ts.tv_nsec >= NSEC) e(91);
	return (int64_t)ts.tv_sec * NSEC + ts.tv_nsec;
}

/* The resolution, and whether the clocks move between ticks. */
static void
test173a(void)
{
	struct timespec res;
	struct timeval tv;
	int64_t t0, t1, d, small;
	int i;

	subtest = 1;

	tick_ns = NSEC / sysconf(_SC_CLK_TCK);
	if (clock_getres(CLOCK_MONOTONIC, &res) != 0) e(1);
	hires = (res.tv_sec == 0 && res.tv_nsec == 1);
	if (!hires && (res.tv_sec != 0 || res.tv_nsec != tick_ns)) e(2);
	if (clock_getres(CLOCK_REALTIME, &res) != 0) e(3);
	if (hires != (res.tv_sec == 0 && res.tv_nsec == 1)) e(4);
	if (!hires) {
		printf("test173: the clocks are tick clocks here, "
		    "subtests 1-5 check that only\n");
		return;
	}

	/* Between two reads, sooner or later, less than a tick. */
	small = 0;
	t0 = now(CLOCK_MONOTONIC);
	for (i = 0; i < 1000000 && !small; i++) {
		t1 = now(CLOCK_MONOTONIC);
		d = t1 - t0;
		if (d < 0) e(5);
		if (d > 0 && d < tick_ns) small = d;
		t0 = t1;
	}
	if (!small) e(6);

	/* gettimeofday() is CLOCK_REALTIME too. */
	t0 = now(CLOCK_REALTIME);
	if (gettimeofday(&tv, NULL) != 0) e(7);
	t1 = now(CLOCK_REALTIME);
	d = (int64_t)tv.tv_sec * NSEC + (int64_t)tv.tv_usec * 1000;
	if (d < t0 - 1000 || d > t1) e(8);
}

/* Never back, on any CPU: each thread's read, made under the lock, is no
 * earlier than the last any thread made. */
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static int64_t last;
static volatile int back;

static void *
reader(void *arg)
{
	int64_t t;
	int i;

	for (i = 0; i < 200000; i++) {
		pthread_mutex_lock(&mtx);
		t = now(*(clockid_t *)arg);
		if (t < last)
			back++;
		last = t;
		pthread_mutex_unlock(&mtx);
		if (i % 1000 == 0)
			sched_yield();
	}
	return NULL;
}

static void
test173b(void)
{
	static clockid_t clks[] = { CLOCK_MONOTONIC, CLOCK_REALTIME };
	pthread_t t[2];
	int c;

	subtest = 2;

	for (c = 0; c < 2; c++) {
		last = 0;
		back = 0;
		if (pthread_create(&t[0], NULL, reader, &clks[c]) != 0) e(1);
		if (pthread_create(&t[1], NULL, reader, &clks[c]) != 0) e(2);
		if (pthread_join(t[0], NULL) != 0) e(3);
		if (pthread_join(t[1], NULL) != 0) e(4);
		if (back != 0) e(5);
	}
}

/* A sleep takes as long by either clock, and at least as long as asked. */
static void
test173c(void)
{
	struct timespec ts;
	int64_t m0, m1, r0, r1;

	subtest = 3;

	ts.tv_sec = 0;
	ts.tv_nsec = 50000000;
	m0 = now(CLOCK_MONOTONIC);
	r0 = now(CLOCK_REALTIME);
	if (nanosleep(&ts, NULL) != 0) e(1);
	m1 = now(CLOCK_MONOTONIC);
	r1 = now(CLOCK_REALTIME);
	if (m1 - m0 < 50000000) e(2);
	if (m1 - m0 > 50000000 + 10 * tick_ns) e(3);
	if ((r1 - r0) - (m1 - m0) > 2 * tick_ns ||
	    (m1 - m0) - (r1 - r0) > 2 * tick_ns) e(4);
}

/* While adjtime() runs realtime fast, then slow, it never runs back. */
static void
test173d(void)
{
	struct timeval delta, zero;
	int64_t t0, t1, start;
	int pass;

	subtest = 4;

	if (geteuid() != 0) {
		printf("test173: not setuid root, subtests 4-5 skipped\n");
		return;
	}
	memset(&zero, 0, sizeof(zero));
	for (pass = 0; pass < 2; pass++) {
		delta.tv_sec = pass == 0 ? 1 : -1;
		delta.tv_usec = 0;
		if (adjtime(&delta, NULL) != 0) e(1);
		start = t0 = now(CLOCK_MONOTONIC);
		t1 = now(CLOCK_REALTIME);
		while (t0 - start < 300000000) {	/* 0.3 s */
			int64_t r = now(CLOCK_REALTIME);
			if (r < t1) e(2);
			t1 = r;
			t0 = now(CLOCK_MONOTONIC);
		}
		if (adjtime(&zero, NULL) != 0) e(3);
	}
}

/* A clock_settime() shows at once, and is undone. */
static void
test173e(void)
{
	struct timespec ts;
	int64_t m0, r0, r1;

	subtest = 5;

	if (geteuid() != 0)
		return;
	m0 = now(CLOCK_MONOTONIC);
	r0 = now(CLOCK_REALTIME);
	ts.tv_sec = (r0 + 1000 * NSEC) / NSEC;
	ts.tv_nsec = (r0 + 1000 * NSEC) % NSEC;
	if (clock_settime(CLOCK_REALTIME, &ts) != 0) e(1);
	r1 = now(CLOCK_REALTIME);
	/* Back as it was, with the time that has gone by. */
	r0 += now(CLOCK_MONOTONIC) - m0;
	ts.tv_sec = r0 / NSEC;
	ts.tv_nsec = r0 % NSEC;
	if (clock_settime(CLOCK_REALTIME, &ts) != 0) e(2);
	if (r1 < r0 + 999 * NSEC || r1 > r0 + 1001 * NSEC) e(3);
	/* The monotonic clock did not move with it. */
	if (now(CLOCK_MONOTONIC) - m0 > NSEC) e(4);
}

int
main(int argc, char **argv)
{

	start(173);

	test173a();
	test173b();
	test173c();
	test173d();
	test173e();

	quit();
	return 0;
}
