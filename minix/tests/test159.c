/* Test 159 - CPU affinity: sched_setaffinity_np(3), pthread_setaffinity_np(3).
 *
 * The set of CPUs a thread may run on: what it is, who gets it (fork(),
 * pthread_create(), exec), who may change it, and that SCHED keeps to it: a
 * time-sharing thread on the same CPU as a real-time one does not run, on
 * another one it does, even if it has to be moved there first.
 *
 * Must be setuid root.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

#define SPIN_MS		300	/* how long the spinner holds its CPU */

static long ncpu;
static volatile int stop, ready;

/* A set of the CPUs in 'mask'. */
static cpuset_t *
make_set(unsigned mask)
{
	cpuset_t *set;
	int i;

	if ((set = cpuset_create()) == NULL) e(90);
	cpuset_zero(set);
	for (i = 0; i < 32; i++)
		if ((mask & (1U << i)) && cpuset_set(i, set) != 0) e(91);
	return set;
}

/* The CPUs in 'set', as a mask. */
static unsigned
set_mask(const cpuset_t *set)
{
	unsigned mask = 0;
	int i;

	for (i = 0; i < 32; i++)
		if (cpuset_isset(i, set) > 0)
			mask |= 1U << i;
	return mask;
}

static int
set_proc(pid_t pid, unsigned mask)
{
	cpuset_t *set = make_set(mask);
	int r;

	r = sched_setaffinity_np(pid, cpuset_size(set), set);
	cpuset_destroy(set);
	return r;
}

static unsigned
get_proc(pid_t pid)
{
	cpuset_t *set = make_set(0);
	unsigned mask;

	if (sched_getaffinity_np(pid, cpuset_size(set), set) != 0) e(92);
	mask = set_mask(set);
	cpuset_destroy(set);
	return mask;
}

static int
set_thread(pthread_t t, unsigned mask)
{
	cpuset_t *set = make_set(mask);
	int r;

	r = pthread_setaffinity_np(t, cpuset_size(set), set);
	cpuset_destroy(set);
	return r;
}

static unsigned
get_thread(pthread_t t)
{
	cpuset_t *set = make_set(0);
	unsigned mask;

	if (pthread_getaffinity_np(t, cpuset_size(set), set) != 0) e(93);
	mask = set_mask(set);
	cpuset_destroy(set);
	return mask;
}

static unsigned all, last;	/* every CPU there is; the highest one */

static void *
report(void *arg)
{
	return (void *)(long) get_thread(pthread_self());
}

static void
test159a(void)
{
	pthread_t t;
	void *res;

	subtest = 1;

	/* Any CPU, to begin with. */
	if (get_proc(0) != all) e(1);
	if (get_proc(getpid()) != all) e(2);
	if (get_thread(pthread_self()) != all) e(3);

	/* One, then any again: none, or all of them, is any. */
	if (set_proc(0, last) != 0) e(4);
	if (get_proc(0) != last) e(5);
	if (set_proc(0, 0) != 0) e(6);
	if (get_proc(0) != all) e(7);
	if (set_proc(0, last) != 0 || set_proc(0, all | 0x80000000U) != 0) e(8);
	if (get_proc(0) != all) e(9);

	/* No CPU that is there. */
	if (all != 0xffffffffU) {
		if (set_proc(0, ~all) != -1 || errno != EINVAL) e(10);
		if (get_proc(0) != all) e(11);
	}
	if (set_proc(-1, last) != -1 || errno != EINVAL) e(12);
	if (set_proc(999999, last) != -1 || errno != ESRCH) e(13);

	/* A thread has its own. */
	if (pthread_create(&t, NULL, report, NULL) != 0) e(14);
	if (pthread_join(t, &res) != 0 || (unsigned)(long) res != all) e(15);
	if (set_thread(pthread_self(), last) != 0) e(16);
	if (get_thread(pthread_self()) != last) e(17);
	if (get_proc(0) != last) e(18);		/* the calling thread's */
	if (set_thread(pthread_self(), 0) != 0) e(19);
	if (get_proc(0) != all) e(20);
}

static void
test159b(char *self)
{
	pthread_t t;
	void *res;
	pid_t pid;
	int status;

	subtest = 2;

	if (set_proc(0, last) != 0) e(1);

	/* pthread_create() and fork() pass it on... */
	if (pthread_create(&t, NULL, report, NULL) != 0) e(2);
	if (pthread_join(t, &res) != 0 || (unsigned)(long) res != last) e(3);
	if ((pid = fork()) == 0)
		_exit(get_proc(0) == last ? 0 : 1);
	if (pid < 0) e(4);
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0) e(5);

	/* ...and so does exec. */
	if ((pid = fork()) == 0) {
		char *av[] = { self, "exec", NULL };

		execv(self, av);
		_exit(2);
	}
	if (pid < 0) e(6);
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0) e(7);

	/* Setting it for a process sets it for all its threads. */
	if (set_thread(pthread_self(), all) != 0) e(8);
	if (pthread_create(&t, NULL, report, NULL) != 0) e(9);
	if (pthread_join(t, &res) != 0 || (unsigned)(long) res != all) e(10);
	if (set_proc(0, 0) != 0) e(11);
}

static void
test159c(void)
{
	pid_t pid, parent;
	int status;

	subtest = 3;

	/* Only for one's own processes, unless root. */
	parent = getpid();
	if ((pid = fork()) == 0) {
		if (setuid(9999) != 0) _exit(1);
		if (set_proc(parent, last) != -1 || errno != EPERM) _exit(2);
		if (get_proc(parent) != all) _exit(3);	/* anyone may look */
		if (set_proc(0, last) != 0) _exit(4);
		if (get_proc(0) != last) _exit(5);
		_exit(0);
	}
	if (pid < 0) e(1);
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0) e(2);
	if (get_proc(0) != all) e(3);
}

static long
ms_now(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) e(94);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000;
}

static void *
spinner(void *arg)
{
	while (!stop)
		;
	return arg;
}

/* Run for SPIN_MS * 3, saying so once the clock is going, and return the
 * longest time the thread did not get the CPU.
 */
static void *
sampler(void *arg)
{
	long start, last_ms, now, gap = 0;

	start = last_ms = ms_now();
	ready = 1;
	while ((now = ms_now()) - start < SPIN_MS * 3) {
		if (now - last_ms > gap)
			gap = now - last_ms;
		last_ms = now;
	}
	return (void *) gap;
}

/* A time-sharing thread on CPU 'mine' (as a mask), and a real-time one
 * spinning on 'theirs' for SPIN_MS: how long was the first kept off?  The
 * first is moved to its CPU once it runs.
 */
static long
gap_with(unsigned mine, unsigned theirs)
{
	pthread_t s, t;
	pthread_attr_t a;
	struct sched_param sp;
	void *res;

	ready = 0;
	stop = 0;
	/* Not the main thread's policy: time-sharing. */
	if (pthread_attr_init(&a) != 0) e(78);
	if (pthread_attr_setinheritsched(&a, PTHREAD_EXPLICIT_SCHED) != 0 ||
	    pthread_attr_setschedpolicy(&a, SCHED_OTHER) != 0) e(79);
	sp.sched_priority = 0;
	if (pthread_attr_setschedparam(&a, &sp) != 0) e(79);
	if (pthread_create(&t, &a, sampler, NULL) != 0) e(80);
	pthread_attr_destroy(&a);
	if (set_thread(t, mine) != 0) e(81);
	while (!ready)
		usleep(1000);

	if (pthread_attr_init(&a) != 0) e(82);
	if (pthread_attr_setinheritsched(&a, PTHREAD_EXPLICIT_SCHED) != 0 ||
	    pthread_attr_setschedpolicy(&a, SCHED_FIFO) != 0) e(83);
	sp.sched_priority = 5;
	if (pthread_attr_setschedparam(&a, &sp) != 0) e(84);
	if (pthread_create(&s, &a, spinner, NULL) != 0) e(85);
	pthread_attr_destroy(&a);
	if (set_thread(s, theirs) != 0) e(86);
	usleep(SPIN_MS * 1000);
	stop = 1;
	if (pthread_join(s, NULL) != 0) e(87);
	if (pthread_join(t, &res) != 0) e(88);
	return (long) res;
}

static void
test159d(void)
{
	struct sched_param sp;

	subtest = 4;

	/* The main thread, above the spinner, ends the spin. */
	sp.sched_priority = 20;
	if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0) e(1);

	/* Same CPU: kept off.  Another one, here the boot CPU, where user
	 * threads do not go by themselves: it runs.
	 */
	if (gap_with(last, last) < SPIN_MS * 2 / 3) e(2);
	if (ncpu > 1 && gap_with(1, last) >= SPIN_MS / 3) e(3);
	if (ncpu > 1 && gap_with(1, 1) < SPIN_MS * 2 / 3) e(4);

	sp.sched_priority = 0;
	if (pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp) != 0) e(5);
}

int
main(int argc, char **argv)
{
	char self[PATH_MAX];

	if ((ncpu = sysconf(_SC_NPROCESSORS_ONLN)) < 1)
		ncpu = 1;
	if (ncpu > 32)
		ncpu = 32;
	all = (ncpu == 32) ? 0xffffffffU : (1U << ncpu) - 1;
	last = 1U << (ncpu - 1);

	/* The exec of test159b. */
	if (argc > 1 && strcmp(argv[1], "exec") == 0)
		return get_proc(0) == last ? 0 : 1;

	/* start() moves to a directory of its own: exec by the full path. */
	if (realpath(argv[0], self) == NULL) {
		printf("realpath %s: %s\n", argv[0], strerror(errno));
		return 1;
	}

	start(159);

	alarm(120);
	test159a();
	test159b(self);
	test159c();
	test159d();
	alarm(0);

	quit();
	return 0;
}
