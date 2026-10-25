/* Test 158 - PTHREAD_PRIO_PROTECT mutexes: the priority ceiling.
 *
 * A thread that holds a PTHREAD_PRIO_PROTECT mutex runs at the mutex's
 * ceiling (POSIX), if it is root: real-time priority is root's, and a mutex
 * is no way around that; anyone else still gets the lock, at its own
 * priority.  The test sees the ceiling at work: real-time threads below it
 * take every CPU for a while, and a time-sharing thread holding the mutex
 * still runs; one holding a plain mutex, or a non-root one, does not.  All on
 * one CPU (sched_setaffinity_np(3)).
 *
 * Must be setuid root.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

#define SPIN_MS		300	/* how long the spinners hold every CPU */
#define SPIN_PRIO	5	/* their priority, below... */
#define CEILING		10	/* ...the ceiling of the mutex, below... */
#define MAIN_PRIO	20	/* ...the main thread, which ends the spin */
#define MAX_SPIN	8

#define RAN(gap)	((gap) < SPIN_MS / 3)	/* not kept off the CPU */
#define STARVED(gap)	((gap) >= SPIN_MS * 2 / 3)	/* kept off it */

static volatile int stop, ready;
static long ncpu;

static long
ms_now(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) e(99);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000;
}

/* Run for 'ms' and return the longest time the caller did not get the CPU.
 * Say it is running only once the clock is going (in 'flag', or on 'fd'):
 * whoever starts the spinners then may well keep it off before its loop.
 */
static long
longest_gap(long ms, volatile int *flag, int fd)
{
	long start, last, now, gap = 0;

	start = last = ms_now();
	if (flag != NULL)
		*flag = 1;
	if (fd >= 0 && write(fd, "r", 1) != 1)
		return -1;
	while ((now = ms_now()) - start < ms) {
		if (now - last > gap)
			gap = now - last;
		last = now;
	}
	return gap;
}

static void *
spinner(void *arg)
{
	while (!stop)
		;
	return arg;
}

/* Take every CPU for SPIN_MS, with real-time threads below the ceiling. */
static void
hog_cpus(void)
{
	pthread_t t[MAX_SPIN];
	pthread_attr_t a;
	struct sched_param sp;
	int i, n;

	n = (ncpu > MAX_SPIN) ? MAX_SPIN : (int) ncpu;
	if (pthread_attr_init(&a) != 0) e(90);
	if (pthread_attr_setinheritsched(&a, PTHREAD_EXPLICIT_SCHED) != 0 ||
	    pthread_attr_setschedpolicy(&a, SCHED_FIFO) != 0) e(91);
	sp.sched_priority = SPIN_PRIO;
	if (pthread_attr_setschedparam(&a, &sp) != 0) e(92);
	stop = 0;
	for (i = 0; i < n; i++)
		if (pthread_create(&t[i], &a, spinner, NULL) != 0) e(93);
	usleep(SPIN_MS * 1000);
	stop = 1;
	for (i = 0; i < n; i++)
		if (pthread_join(t[i], NULL) != 0) e(94);
	pthread_attr_destroy(&a);
}

static void
set_main(int policy, int prio)
{
	struct sched_param sp;

	sp.sched_priority = prio;
	if (pthread_setschedparam(pthread_self(), policy, &sp) != 0) e(95);
}

static void
make_mutex(pthread_mutex_t *m, int protocol, int ceiling)
{
	pthread_mutexattr_t ma;

	if (pthread_mutexattr_init(&ma) != 0) e(96);
	if (pthread_mutexattr_setprotocol(&ma, protocol) != 0) e(97);
	if (protocol == PTHREAD_PRIO_PROTECT &&
	    pthread_mutexattr_setprioceiling(&ma, ceiling) != 0) e(98);
	if (pthread_mutex_init(m, &ma) != 0) e(96);
	pthread_mutexattr_destroy(&ma);
}

static pthread_mutex_t *held;
static int use_trylock;
static long gap;

static void *
holder(void *arg)
{
	int r;

	r = use_trylock ? pthread_mutex_trylock(held) :
	    pthread_mutex_lock(held);
	if (r != 0) e(80);
	gap = longest_gap(SPIN_MS * 3, &ready, -1);
	if (pthread_mutex_unlock(held) != 0) e(81);
	return arg;
}

/* A time-sharing thread takes 'm' and runs while the spinners have every
 * CPU: return the longest time it was kept off.
 */
static long
held_gap(pthread_mutex_t *m, int trylock)
{
	pthread_t t;
	pthread_attr_t a;
	struct sched_param sp;
	int policy;

	held = m;
	use_trylock = trylock;
	ready = 0;
	if (pthread_attr_init(&a) != 0) e(82);
	if (pthread_attr_setinheritsched(&a, PTHREAD_EXPLICIT_SCHED) != 0 ||
	    pthread_attr_setschedpolicy(&a, SCHED_OTHER) != 0) e(83);
	sp.sched_priority = 0;
	if (pthread_attr_setschedparam(&a, &sp) != 0) e(84);
	if (pthread_create(&t, &a, holder, NULL) != 0) e(85);
	pthread_attr_destroy(&a);
	while (!ready)
		usleep(1000);
	hog_cpus();
	/* The ceiling is not its own policy. */
	if (pthread_getschedparam(t, &policy, &sp) != 0) e(86);
	if (policy != SCHED_OTHER || sp.sched_priority != 0) e(87);
	if (pthread_join(t, NULL) != 0) e(88);
	return gap;
}

static void
test158a(void)
{
	pthread_mutex_t plain, prot;
	int ceil;

	subtest = 1;

	make_mutex(&plain, PTHREAD_PRIO_NONE, 0);
	make_mutex(&prot, PTHREAD_PRIO_PROTECT, CEILING);
	set_main(SCHED_FIFO, MAIN_PRIO);

	/* Without a ceiling, the spinners keep the thread off: the test can
	 * tell. */
	if (!STARVED(held_gap(&plain, 0))) e(1);
	/* At the ceiling, it runs.  Twice: a PROTECT mutex stays one once
	 * unlocked. */
	if (!RAN(held_gap(&prot, 0))) e(2);
	if (!RAN(held_gap(&prot, 0))) e(3);
	if (pthread_mutex_getprioceiling(&prot, &ceil) != 0 || ceil != CEILING)
		e(4);
	/* pthread_mutex_trylock() takes it at the ceiling too. */
	if (!RAN(held_gap(&prot, 1))) e(5);
	/* And once unlocked, the thread is back to its own priority. */
	if (!STARVED(held_gap(&plain, 0))) e(6);

	set_main(SCHED_OTHER, 0);
	pthread_mutex_destroy(&plain);
	pthread_mutex_destroy(&prot);
}

static void
test158b(void)
{
	pthread_mutex_t prot;
	int fd[2];
	pid_t pid;
	long g;
	char c;
	int status;

	subtest = 2;

	/* Not root: the lock is taken, at the thread's own priority. */
	if (pipe(fd) != 0) e(1);
	set_main(SCHED_FIFO, MAIN_PRIO);
	if ((pid = fork()) == 0) {
		set_main(SCHED_OTHER, 0);
		if (setuid(9999) != 0) _exit(1);
		make_mutex(&prot, PTHREAD_PRIO_PROTECT, CEILING);
		if (pthread_mutex_lock(&prot) != 0) _exit(2);
		if ((g = longest_gap(SPIN_MS * 3, NULL, fd[1])) < 0) _exit(3);
		if (pthread_mutex_unlock(&prot) != 0) _exit(4);
		if (pthread_mutex_trylock(&prot) != 0) _exit(5);
		if (pthread_mutex_unlock(&prot) != 0) _exit(6);
		if (write(fd[1], &g, sizeof(g)) != sizeof(g)) _exit(7);
		_exit(0);
	}
	if (pid < 0) e(2);
	if (read(fd[0], &c, 1) != 1) e(3);
	hog_cpus();
	if (read(fd[0], &g, sizeof(g)) != sizeof(g)) e(4);
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0) e(5);
	if (!STARVED(g)) e(6);
	set_main(SCHED_OTHER, 0);
	close(fd[0]);
	close(fd[1]);
}

static void
test158c(void)
{
	pthread_mutex_t prot, prot2;

	subtest = 3;

	make_mutex(&prot, PTHREAD_PRIO_PROTECT, CEILING);
	make_mutex(&prot2, PTHREAD_PRIO_PROTECT, CEILING + 5);

	/* A thread above the ceiling may not lock it (POSIX: EINVAL). */
	set_main(SCHED_FIFO, MAIN_PRIO);
	if (pthread_mutex_lock(&prot) != EINVAL) e(1);
	if (pthread_mutex_trylock(&prot) != EINVAL) e(2);
	set_main(SCHED_OTHER, 0);

	/* Nested, in either order of unlocking; then all is as before. */
	if (pthread_mutex_lock(&prot) != 0) e(3);
	if (pthread_mutex_lock(&prot2) != 0) e(4);
	if (pthread_mutex_unlock(&prot) != 0) e(5);
	if (pthread_mutex_unlock(&prot2) != 0) e(6);
	if (pthread_mutex_lock(&prot) != 0) e(7);
	if (pthread_mutex_unlock(&prot) != 0) e(8);
	if (pthread_mutex_trylock(&prot) != 0) e(9);
	if (pthread_mutex_trylock(&prot) != EBUSY) e(10);
	if (pthread_mutex_unlock(&prot) != 0) e(11);

	pthread_mutex_destroy(&prot);
	pthread_mutex_destroy(&prot2);
}

int
main(int argc, char **argv)
{
	cpuset_t *set;

	start(158);

	/* All on one CPU, so that the spinners keep a thread off for sure:
	 * with more, SCHED may move it to the boot CPU, which they do not go
	 * to.  The highest: the boot CPU runs the system services.
	 */
	if ((ncpu = sysconf(_SC_NPROCESSORS_ONLN)) < 1)
		ncpu = 1;
	if ((set = cpuset_create()) == NULL) e(1);
	cpuset_zero(set);
	if (cpuset_set(ncpu - 1, set) != 0) e(2);
	if (sched_setaffinity_np(0, cpuset_size(set), set) != 0) e(3);
	cpuset_destroy(set);
	ncpu = 1;

	alarm(120);
	test158a();
	test158b();
	test158c();
	alarm(0);

	quit();
	return 0;
}
