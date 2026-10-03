/* Test 118 - POSIX per-process timers: timer_create(2) and co.
 *
 * Signals carry SI_TIMER and the sigev_value, periodic timers keep their
 * rate, expiries while the signal is blocked become overruns, absolute times
 * work, the limits and errors are as POSIX says, and timers are neither
 * inherited by fork(2) nor kept across exec(2).  The clock tick (1/60 s) is
 * the resolution, so the timing checks allow for it.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

static volatile sig_atomic_t got, got_code, got_signo;
static void * volatile got_value;
static int marker;

static void
handler(int sig, siginfo_t *si, void *ctx)
{

	got++;
	got_signo = si->si_signo;
	got_code = si->si_code;
	got_value = si->si_value.sival_ptr;
}

static void
catch(int sig)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = handler;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	if (sigaction(sig, &sa, NULL) != 0) e(90);
}

static void
ts_set(struct timespec *ts, long ms)
{

	ts->tv_sec = ms / 1000;
	ts->tv_nsec = (ms % 1000) * 1000000L;
}

static long
ts_ms(const struct timespec *ts)
{

	return ts->tv_sec * 1000 + ts->tv_nsec / 1000000L;
}

static long
now_ms(clockid_t clk)
{
	struct timespec ts;

	if (clock_gettime(clk, &ts) != 0) e(91);
	return ts_ms(&ts);
}

/* Wait for 'n' signals or until 'ms' have passed. */
static void
wait_for(int n, long ms)
{
	long end = now_ms(CLOCK_MONOTONIC) + ms;

	while (got < n && now_ms(CLOCK_MONOTONIC) < end)
		usleep(5000);
}

static timer_t
make(clockid_t clk, int notify, int sig, void *value)
{
	struct sigevent ev;
	timer_t t;

	memset(&ev, 0, sizeof(ev));
	ev.sigev_notify = notify;
	ev.sigev_signo = sig;
	ev.sigev_value.sival_ptr = value;
	if (timer_create(clk, &ev, &t) != 0) e(92);
	return t;
}

static void
arm(timer_t t, int flags, long value_ms, long interval_ms)
{
	struct itimerspec its;

	ts_set(&its.it_value, value_ms);
	ts_set(&its.it_interval, interval_ms);
	if (timer_settime(t, flags, &its, NULL) != 0) e(93);
}

static void
test_oneshot(void)
{
	struct itimerspec its;
	timer_t t;
	long t0;

	subtest = 1;
	catch(SIGUSR1);
	got = 0;
	t = make(CLOCK_MONOTONIC, SIGEV_SIGNAL, SIGUSR1, &marker);
	t0 = now_ms(CLOCK_MONOTONIC);
	arm(t, 0, 100, 0);
	wait_for(1, 2000);
	if (got != 1) e(1);
	if (now_ms(CLOCK_MONOTONIC) - t0 < 100 - 20) e(2);	/* not early */
	if (got_signo != SIGUSR1 || got_code != SI_TIMER) e(3);
	if (got_value != &marker) e(4);
	if (timer_gettime(t, &its) != 0) e(5);
	if (ts_ms(&its.it_value) != 0 || ts_ms(&its.it_interval) != 0) e(6);
	usleep(200000);
	if (got != 1) e(7);				/* one-shot */
	if (timer_delete(t) != 0) e(8);
}

static void
test_periodic(void)
{
	timer_t t;

	subtest = 2;
	got = 0;
	t = make(CLOCK_REALTIME, SIGEV_SIGNAL, SIGUSR1, &marker);
	arm(t, 0, 50, 50);
	wait_for(1000, 1000);		/* one second (usleep() takes less) */
	if (timer_delete(t) != 0) e(1);
	/* 20 expected; allow for the 1/60 s tick and a busy host. */
	if (got < 12 || got > 24) {
		printf("periodic: %d signals in 1 s\n", (int)got);
		e(2);
	}
}

static void
test_overrun(void)
{
	sigset_t set, old;
	timer_t t;
	int ov;

	subtest = 3;
	got = 0;
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	if (sigprocmask(SIG_BLOCK, &set, &old) != 0) e(1);
	t = make(CLOCK_MONOTONIC, SIGEV_SIGNAL, SIGUSR1, &marker);
	arm(t, 0, 20, 20);
	usleep(400000);					/* ~20 expiries */
	arm(t, 0, 0, 0);				/* disarm */
	if (sigprocmask(SIG_SETMASK, &old, NULL) != 0) e(2);
	if (got != 1) e(3);				/* one signal */
	ov = timer_getoverrun(t);
	if (ov < 8) {
		printf("overrun: %d\n", ov);
		e(4);
	}
	if (timer_delete(t) != 0) e(5);
}

static void
test_gettime(void)
{
	struct itimerspec its, old;
	timer_t t;
	long v;

	subtest = 4;
	t = make(CLOCK_MONOTONIC, SIGEV_NONE, 0, NULL);
	arm(t, 0, 10000, 3000);
	usleep(100000);
	if (timer_gettime(t, &its) != 0) e(1);
	v = ts_ms(&its.it_value);
	if (v > 10000 || v < 9000) e(2);
	if (ts_ms(&its.it_interval) < 2950 || ts_ms(&its.it_interval) > 3050)
		e(3);
	/* Disarming reports the old setting. */
	memset(&its, 0, sizeof(its));
	if (timer_settime(t, 0, &its, &old) != 0) e(4);
	if (ts_ms(&old.it_value) < 9000) e(5);
	if (timer_gettime(t, &its) != 0) e(6);
	if (ts_ms(&its.it_value) != 0) e(7);

	subtest = 5;
	/* SIGEV_NONE runs down without a signal. */
	got = 0;
	arm(t, 0, 50, 0);
	usleep(200000);
	if (got != 0) e(1);
	if (timer_delete(t) != 0) e(2);
}

static void
test_abstime(void)
{
	struct timespec ts;
	struct itimerspec its;
	timer_t t;

	subtest = 6;
	got = 0;
	t = make(CLOCK_REALTIME, SIGEV_SIGNAL, SIGUSR1, &marker);
	if (clock_gettime(CLOCK_REALTIME, &ts) != 0) e(1);
	ts.tv_nsec += 200000000L;
	if (ts.tv_nsec >= 1000000000L) {
		ts.tv_sec++;
		ts.tv_nsec -= 1000000000L;
	}
	memset(&its, 0, sizeof(its));
	its.it_value = ts;
	if (timer_settime(t, TIMER_ABSTIME, &its, NULL) != 0) e(2);
	usleep(50000);
	if (got != 0) e(3);				/* not yet */
	wait_for(1, 2000);
	if (got != 1) e(4);

	/* A time that has passed expires at once. */
	got = 0;
	its.it_value.tv_sec -= 100;
	if (timer_settime(t, TIMER_ABSTIME, &its, NULL) != 0) e(5);
	wait_for(1, 1000);
	if (got != 1) e(6);
	if (timer_delete(t) != 0) e(7);
}

static void
test_default(void)
{
	timer_t t;

	subtest = 7;
	/* No sigevent: SIGALRM, with the timer id as the value. */
	catch(SIGALRM);
	got = 0;
	if (timer_create(CLOCK_MONOTONIC, NULL, &t) != 0) e(1);
	arm(t, 0, 30, 0);
	wait_for(1, 2000);
	if (got != 1 || got_signo != SIGALRM) e(2);
	if ((timer_t)(intptr_t)got_value != t) e(3);
	if (timer_delete(t) != 0) e(4);
}

static void
test_errors(void)
{
	struct sigevent ev;
	struct itimerspec its;
	timer_t t, many[32];
	int i, n;

	subtest = 8;
	memset(&ev, 0, sizeof(ev));
	ev.sigev_notify = SIGEV_SIGNAL;
	ev.sigev_signo = SIGUSR1;
	if (timer_create(12345, &ev, &t) != -1 || errno != EINVAL) e(1);
	ev.sigev_notify = SIGEV_THREAD;
	if (timer_create(CLOCK_REALTIME, &ev, &t) != -1 || errno != EINVAL)
		e(2);
	ev.sigev_notify = SIGEV_SIGNAL;
	ev.sigev_signo = 0;
	if (timer_create(CLOCK_REALTIME, &ev, &t) != -1 || errno != EINVAL)
		e(3);
	if (timer_delete(1000) != -1 || errno != EINVAL) e(4);
	if (timer_gettime(1000, &its) != -1 || errno != EINVAL) e(5);

	t = make(CLOCK_REALTIME, SIGEV_NONE, 0, NULL);
	memset(&its, 0, sizeof(its));
	its.it_value.tv_nsec = 1000000000L;
	if (timer_settime(t, 0, &its, NULL) != -1 || errno != EINVAL) e(6);
	if (timer_delete(t) != 0) e(7);
	if (timer_delete(t) != -1 || errno != EINVAL) e(8);	/* gone */

	subtest = 9;
	/* 32 timers per process, then EAGAIN. */
	ev.sigev_notify = SIGEV_NONE;
	for (n = 0; n < 32; n++)
		if (timer_create(CLOCK_REALTIME, &ev, &many[n]) != 0) break;
	if (n != 32) e(1);
	if (timer_create(CLOCK_REALTIME, &ev, &t) != -1 || errno != EAGAIN)
		e(2);
	for (i = 0; i < n; i++)
		if (timer_delete(many[i]) != 0) e(3);
}

static void
test_lifetime(const char *self)
{
	struct itimerspec its;
	char arg[16];
	timer_t t;
	pid_t pid;
	int status;

	subtest = 10;
	t = make(CLOCK_MONOTONIC, SIGEV_NONE, 0, NULL);
	arm(t, 0, 10000, 0);

	/* fork: the child has no timers. */
	if ((pid = fork()) < 0) e(1);
	if (pid == 0)
		_exit(timer_gettime(t, &its) == -1 && errno == EINVAL ? 0 : 1);
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0) e(2);

	/* exec: a process's timers are gone in its new image. */
	if ((pid = fork()) < 0) e(3);
	if (pid == 0) {
		timer_t ct = make(CLOCK_MONOTONIC, SIGEV_NONE, 0, NULL);

		arm(ct, 0, 10000, 0);
		if (timer_gettime(ct, &its) != 0) _exit(3);
		snprintf(arg, sizeof(arg), "%d", (int)ct);
		execl(self, self, "exec-check", arg, (char *)NULL);
		_exit(2);
	}
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0) {
		printf("exec check: status 0x%x\n", status);
		e(4);
	}

	if (timer_gettime(t, &its) != 0 || ts_ms(&its.it_value) < 9000) e(5);
	if (timer_delete(t) != 0) e(6);
}

int
main(int argc, char **argv)
{
	static char self[1024];
	struct itimerspec its;

	if (argc == 3 && !strcmp(argv[1], "exec-check")) {
		/* The timer the parent had before exec must not exist. */
		exit(timer_gettime((timer_t)atoi(argv[2]), &its) == -1 &&
		    errno == EINVAL ? 0 : 1);
	}

	/* start() changes directory: keep an absolute path to ourselves. */
	if (argv[0][0] == '/')
		strlcpy(self, argv[0], sizeof(self));
	else if (getcwd(self, sizeof(self)) != NULL) {
		strlcat(self, "/", sizeof(self));
		strlcat(self, argv[0], sizeof(self));
	}

	start(118);

	test_oneshot();
	test_periodic();
	test_overrun();
	test_gettime();
	test_abstime();
	test_default();
	test_errors();
	test_lifetime(self);

	quit();
	return 0;
}
