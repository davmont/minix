/* Test 116 - POSIX process scheduling: sched_*(2).
 *
 * MINIX offers SCHED_OTHER only, at priority 0: the calls report and accept
 * that, refuse SCHED_FIFO and SCHED_RR with EINVAL, check the target process
 * (ESRCH, and EPERM to change another user's), and sched_yield() is a real
 * system call.
 *
 * "test116 measure" compares how much a CPU-bound child gets done while this
 * process spins and while it calls sched_yield(), which shows the yield on a
 * single CPU; it is not part of the test.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

#define NOBODY	9999
#define NOPID	0x7ffffff0

static void
test_policy(void)
{
	struct sched_param sp;
	struct timespec ts;

	subtest = 1;
	if (sched_get_priority_min(SCHED_OTHER) != 0) e(1);
	if (sched_get_priority_max(SCHED_OTHER) != 0) e(2);
	if (sched_get_priority_min(SCHED_FIFO) != -1 || errno != EINVAL) e(3);
	if (sched_get_priority_max(SCHED_RR) != -1 || errno != EINVAL) e(4);
	if (sched_get_priority_max(1234) != -1 || errno != EINVAL) e(5);

	subtest = 2;
	if (sched_getscheduler(0) != SCHED_OTHER) e(1);
	if (sched_getscheduler(getpid()) != SCHED_OTHER) e(2);
	if (sched_getscheduler(1) != SCHED_OTHER) e(3);	/* init */
	if (sched_getscheduler(NOPID) != -1 || errno != ESRCH) e(4);
	if (sched_getscheduler(-1) != -1 || errno != EINVAL) e(5);
	memset(&sp, 0xff, sizeof(sp));
	if (sched_getparam(0, &sp) != 0 || sp.sched_priority != 0) e(6);
	if (sched_getparam(0, NULL) != -1 || errno != EINVAL) e(7);
	if (sched_getparam(NOPID, &sp) != -1 || errno != ESRCH) e(8);

	subtest = 3;
	sp.sched_priority = 0;
	if (sched_setparam(0, &sp) != 0) e(1);
	if (sched_setscheduler(0, SCHED_OTHER, &sp) != SCHED_OTHER) e(2);
	sp.sched_priority = 5;
	if (sched_setparam(0, &sp) != -1 || errno != EINVAL) e(3);
	sp.sched_priority = 0;
	if (sched_setscheduler(0, SCHED_FIFO, &sp) != -1 || errno != EINVAL)
		e(4);
	if (sched_setscheduler(0, SCHED_RR, &sp) != -1 || errno != EINVAL)
		e(5);
	if (sched_setparam(NOPID, &sp) != -1 || errno != ESRCH) e(6);
	if (sched_setparam(0, NULL) != -1 || errno != EINVAL) e(7);

	subtest = 4;
	if (sched_rr_get_interval(0, &ts) != 0) e(1);
	if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000L ||
	    (ts.tv_sec == 0 && ts.tv_nsec == 0)) e(2);
	if (sched_rr_get_interval(NOPID, &ts) != -1 || errno != ESRCH) e(3);

	if (sched_yield() != 0) e(4);
}

static void
test_perm(void)
{
	struct sched_param sp;
	pid_t pid, parent;
	int status;

	subtest = 5;
	/* Another user's process (the parent, which runs as root) can be
	 * inspected but not changed.
	 */
	parent = getpid();
	if ((pid = fork()) < 0) e(1);
	if (pid == 0) {
		memset(&sp, 0, sizeof(sp));
		if (setuid(NOBODY) != 0) _exit(1);
		if (sched_getscheduler(parent) != SCHED_OTHER) _exit(2);
		if (sched_getparam(parent, &sp) != 0) _exit(3);
		if (sched_setparam(parent, &sp) != -1 || errno != EPERM)
			_exit(4);
		if (sched_setscheduler(parent, SCHED_OTHER, &sp) != -1 ||
		    errno != EPERM) _exit(5);
		if (sched_setparam(0, &sp) != 0) _exit(6);	/* own: fine */
		_exit(0);
	}
	if (waitpid(pid, &status, 0) != pid) e(2);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		printf("child status 0x%x\n", status);
		e(3);
	}
}

static void
test_thread(void)
{
	struct sched_param sp;
	int policy;

	subtest = 6;
	/* The per-thread calls follow the same rules. */
	memset(&sp, 0xff, sizeof(sp));
	if (pthread_getschedparam(pthread_self(), &policy, &sp) != 0) e(1);
	if (policy != SCHED_OTHER || sp.sched_priority != 0) e(2);
	sp.sched_priority = 0;
	if (pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp) != 0) e(3);
	if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != EINVAL)
		e(4);
	if (pthread_setschedprio(pthread_self(), 0) != 0) e(5);
	if (pthread_setschedprio(pthread_self(), 3) != EINVAL) e(6);
}

static double
now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static volatile sig_atomic_t report;

static void
on_usr1(int sig)
{

	report = 1;
}

/* Let a CPU-bound child count for two seconds while this process spins
 * (phase A) or calls sched_yield() in a loop (phase B), and return the
 * child's count.
 */
static unsigned long
phase(pid_t pid, int fd, int yield)
{
	volatile unsigned long spin;
	unsigned long count;
	double end;

	/* Check the clock seldom: clock_gettime() is itself a system call,
	 * and would give the CPU away as well.
	 */
	end = now() + 2;
	while (now() < end) {
		if (yield) {
			sched_yield();	/* may hand over a whole quantum */
		} else {
			for (spin = 0; spin < 20000000; spin++)
				;
		}
	}
	kill(pid, SIGUSR1);
	if (read(fd, &count, sizeof(count)) != sizeof(count)) exit(1);
	return count;
}

static void
measure(void)
{
	unsigned long count, a, b;
	int p[2];
	pid_t pid;

	if (pipe(p) != 0) exit(1);
	signal(SIGUSR1, on_usr1);
	if ((pid = fork()) == 0) {
		for (count = 0;; count++)
			if (report) {
				(void)write(p[1], &count, sizeof(count));
				count = 0;
				report = 0;
			}
	}
	a = phase(pid, p[0], 0);
	b = phase(pid, p[0], 1);
	printf("child count: parent spinning %lu, parent yielding %lu "
	    "(%.2fx)\n", a, b, a ? (double)b / a : 0.0);
	kill(pid, SIGKILL);
	waitpid(pid, NULL, 0);
	exit(0);
}

int
main(int argc, char **argv)
{

	if (argc > 1 && !strcmp(argv[1], "measure"))
		measure();

	start(116);

	test_policy();
	test_perm();
	test_thread();

	quit();
	return 0;
}
