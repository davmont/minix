/* Test 119 - CPU time clocks: CLOCK_PROCESS_CPUTIME_ID, CLOCK_THREAD_CPUTIME_ID,
 * clock_getcpuclockid(3) and pthread_getcpuclockid(3).
 *
 * The clocks start near zero, grow by the CPU time used (not by sleeping),
 * the process clock adds up all threads, also those that have exited, and
 * another process's clock can be read through clock_getcpuclockid().  The
 * resolution is the clock tick (1/60 s), which the checks allow for.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

static long
ms(clockid_t clk)
{
	struct timespec ts;

	if (clock_gettime(clk, &ts) != 0) {
		e(90);
		return -1;
	}
	return ts.tv_sec * 1000 + ts.tv_nsec / 1000000L;
}

/* Use about 'cpu_ms' of CPU time on this thread. */
static void
burn(long cpu_ms)
{
	volatile unsigned long n;
	long end;

	end = ms(CLOCK_THREAD_CPUTIME_ID) + cpu_ms;
	while (ms(CLOCK_THREAD_CPUTIME_ID) < end)
		for (n = 0; n < 1000000; n++)
			;
}

static void *
burner(void *arg)
{

	burn((long)arg);
	return NULL;
}

static void
test_process(void)
{
	struct timespec res;
	long p0, p1, w0, w1;

	subtest = 1;
	if (clock_getres(CLOCK_PROCESS_CPUTIME_ID, &res) != 0) e(1);
	if (res.tv_sec != 0 || res.tv_nsec <= 0) e(2);
	if (clock_getres(CLOCK_THREAD_CPUTIME_ID, &res) != 0) e(3);

	/* Sleeping does not count; running does. */
	p0 = ms(CLOCK_PROCESS_CPUTIME_ID);
	if (p0 < 0 || p0 > 60000) e(4);
	usleep(300000);
	p1 = ms(CLOCK_PROCESS_CPUTIME_ID);
	if (p1 - p0 > 100) e(5);

	w0 = ms(CLOCK_MONOTONIC);
	burn(300);
	w1 = ms(CLOCK_MONOTONIC);
	p1 = ms(CLOCK_PROCESS_CPUTIME_ID);
	if (p1 - p0 < 280) e(6);
	if (p1 - p0 > (w1 - w0) + 50) e(7);	/* no more than wall time */
}

static void
test_threads(void)
{
	pthread_t t;
	clockid_t clk;
	long p0, p1, t1, own0, own1;

	subtest = 2;
	/* A thread's clock counts that thread only; the process clock all
	 * threads, also after they exit.
	 */
	p0 = ms(CLOCK_PROCESS_CPUTIME_ID);
	own0 = ms(CLOCK_THREAD_CPUTIME_ID);
	if (pthread_create(&t, NULL, burner, (void *)300L) != 0) e(1);
	if (pthread_getcpuclockid(t, &clk) != 0) e(2);
	usleep(100000);
	while ((t1 = ms(clk)) < 150)		/* the thread's own clock */
		usleep(20000);
	if (pthread_join(t, NULL) != 0) e(3);
	own1 = ms(CLOCK_THREAD_CPUTIME_ID);
	p1 = ms(CLOCK_PROCESS_CPUTIME_ID);
	if (own1 - own0 > 150) e(4);		/* we mostly slept */
	if (p1 - p0 < 280) e(5);		/* the thread's time counts */

	/* Our own clock through pthread_getcpuclockid() is ours. */
	if (pthread_getcpuclockid(pthread_self(), &clk) != 0) e(6);
	own0 = ms(clk);
	burn(100);
	if (ms(clk) - own0 < 80) e(7);
}

static void
test_other(void)
{
	clockid_t clk;
	pid_t pid;
	int p[2], status;
	char c;
	long v;

	subtest = 3;
	if (clock_getcpuclockid(0, &clk) != 0) e(1);
	if (clk != CLOCK_PROCESS_CPUTIME_ID) e(2);
	if (clock_getcpuclockid(getpid(), &clk) != 0) e(3);
	if (clock_getcpuclockid(0x7ffff00, &clk) != ESRCH) e(4);

	/* A child that burns CPU time, read from here. */
	if (pipe(p) != 0) e(5);
	if ((pid = fork()) < 0) e(6);
	if (pid == 0) {
		close(p[0]);
		burn(300);
		(void)write(p[1], "x", 1);
		pause();
		_exit(0);
	}
	close(p[1]);
	if (read(p[0], &c, 1) != 1) e(7);
	if (clock_getcpuclockid(pid, &clk) != 0) e(8);
	v = ms(clk);
	if (v < 280 || v > 5000) e(9);
	kill(pid, SIGKILL);
	if (waitpid(pid, &status, 0) != pid) e(10);
	close(p[0]);

	subtest = 4;
	errno = 0;
	{
		struct timespec ts;

		if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID | 0x1ffffff0, &ts) !=
		    -1 || errno != EINVAL) e(1);
		if (clock_gettime(CLOCK_THREAD_CPUTIME_ID | 0x1ffffff0, &ts) !=
		    -1 || errno != EINVAL) e(2);
	}
}

int
main(int argc, char **argv)
{

	start(119);

	test_process();
	test_threads();
	test_other();

	quit();
	return 0;
}
