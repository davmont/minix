/* Test 160 - PTHREAD_PRIO_INHERIT mutexes: priority inheritance.
 *
 * A thread that holds a PTHREAD_PRIO_INHERIT mutex runs at the priority of
 * the highest of the threads waiting for it (POSIX), until it lets go, and
 * so on down a chain of threads each waiting for the next.  The test sees it
 * at work: a real-time thread takes the CPU for a while, below the waiter's
 * priority, and a time-sharing thread holding the mutex still runs; one
 * holding a plain mutex, or for which only a time-sharing thread waits, or
 * which has let go, does not.  All on one CPU (sched_setaffinity_np(3)).
 *
 * Must be setuid root.
 */
#include <sys/types.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

#define SPIN_MS		300	/* how long the spinner holds the CPU */
#define SPIN_PRIO	5	/* its priority, below... */
#define WAIT_PRIO	10	/* ...the waiter's, below... */
#define MAIN_PRIO	20	/* ...the main thread, which ends the spin */

#define RAN(gap)	((gap) < SPIN_MS / 3)	/* not kept off the CPU */
#define STARVED(gap)	((gap) >= SPIN_MS * 2 / 3)	/* kept off it */

static volatile int stop, ready, go, waiting;
static pthread_mutex_t *held, *wanted;
static long gap;

static long
ms_now(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) e(99);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000;
}

/* Run until 'ms' after 'start' and return the longest time the caller did
 * not get the CPU, counting from 'start'.
 */
static long
longest_gap(long start, long ms)
{
	long last = start, now, g = 0;

	while ((now = ms_now()) - start < ms) {
		if (now - last > g)
			g = now - last;
		last = now;
	}
	return g;
}

static void *
spinner(void *arg)
{
	while (!stop)
		;
	return arg;
}

/* Take the CPU for SPIN_MS with a real-time thread below the waiter. */
static void
hog_cpu(void)
{
	pthread_t t;
	pthread_attr_t a;
	struct sched_param sp;

	if (pthread_attr_init(&a) != 0) e(90);
	if (pthread_attr_setinheritsched(&a, PTHREAD_EXPLICIT_SCHED) != 0 ||
	    pthread_attr_setschedpolicy(&a, SCHED_FIFO) != 0) e(91);
	sp.sched_priority = SPIN_PRIO;
	if (pthread_attr_setschedparam(&a, &sp) != 0) e(92);
	stop = 0;
	if (pthread_create(&t, &a, spinner, NULL) != 0) e(93);
	usleep(SPIN_MS * 1000);
	stop = 1;
	if (pthread_join(t, NULL) != 0) e(94);
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
make_mutex(pthread_mutex_t *m, int protocol, int type)
{
	pthread_mutexattr_t ma;

	if (pthread_mutexattr_init(&ma) != 0) e(96);
	if (pthread_mutexattr_setprotocol(&ma, protocol) != 0) e(97);
	if (pthread_mutexattr_settype(&ma, type) != 0) e(97);
	if (pthread_mutex_init(m, &ma) != 0) e(98);
	pthread_mutexattr_destroy(&ma);
}

/* A thread of 'policy' at 'prio' running 'func'. */
static pthread_t
start_thread(void *(*func)(void *), int policy, int prio, void *arg)
{
	pthread_t t;
	pthread_attr_t a;
	struct sched_param sp;

	if (pthread_attr_init(&a) != 0) e(80);
	if (pthread_attr_setinheritsched(&a, PTHREAD_EXPLICIT_SCHED) != 0 ||
	    pthread_attr_setschedpolicy(&a, policy) != 0) e(81);
	sp.sched_priority = prio;
	if (pthread_attr_setschedparam(&a, &sp) != 0) e(82);
	if (pthread_create(&t, &a, func, arg) != 0) e(83);
	pthread_attr_destroy(&a);
	return t;
}

/* Takes 'held' and runs, until told to let go, or for SPIN_MS * 3. */
static void *
holder(void *arg)
{
	long start;

	if (pthread_mutex_lock(held) != 0) e(70);
	ready = 1;
	if (arg != NULL) {
		/* Let go, then see whether it still runs. */
		while (!go)
			;
		start = ms_now();
		if (pthread_mutex_unlock(held) != 0) e(71);
		gap = longest_gap(start, SPIN_MS * 3);
	} else {
		gap = longest_gap(ms_now(), SPIN_MS * 3);
		if (pthread_mutex_unlock(held) != 0) e(71);
	}
	return NULL;
}

/* An absolute CLOCK_REALTIME time 'ms' from now. */
static void
after(struct timespec *ts, long ms)
{
	if (clock_gettime(CLOCK_REALTIME, ts) != 0) e(78);
	ts->tv_sec += ms / 1000;
	ts->tv_nsec += (ms % 1000) * 1000000;
	if (ts->tv_nsec >= 1000000000) {
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000;
	}
}

/* Waits for 'wanted', which a thread of lower priority holds; with 'arg',
 * only for a while.
 */
static void *
waiter(void *arg)
{
	struct timespec ts;

	waiting = 1;
	if (arg != NULL) {
		after(&ts, 50);
		if (pthread_mutex_timedlock(wanted, &ts) != ETIMEDOUT) e(79);
		return NULL;
	}
	if (pthread_mutex_lock(wanted) != 0) e(72);
	if (pthread_mutex_unlock(wanted) != 0) e(73);
	return NULL;
}

/* Both hold a mutex: 'held' and then 'wanted', for which it waits. */
static void *
middle(void *arg)
{
	if (pthread_mutex_lock((pthread_mutex_t *)arg) != 0) e(74);
	waiting = 1;
	if (pthread_mutex_lock(held) != 0) e(75);
	if (pthread_mutex_unlock(held) != 0) e(76);
	if (pthread_mutex_unlock((pthread_mutex_t *)arg) != 0) e(77);
	return NULL;
}

#define HOLD	0	/* the holder holds on */
#define LETGO	1	/* it lets go as the spinner starts */
#define GIVEUP	2	/* the waiter gives up before */

/* A time-sharing thread holds 'm' while one of 'policy' at 'prio' waits for
 * it and the spinner has the CPU: return the longest time the first was kept
 * off.  With LETGO, the holder lets go as the spinner starts, and the time
 * counts from then.
 */
static long
held_gap(pthread_mutex_t *m, int policy, int prio, int how)
{
	pthread_t h, w;
	struct sched_param sp;
	int pol;

	held = wanted = m;
	ready = go = waiting = 0;
	h = start_thread(holder, SCHED_OTHER, 0, how == LETGO ? (void *)1 :
	    NULL);
	while (!ready)
		usleep(1000);
	w = start_thread(waiter, policy, prio, how == GIVEUP ? (void *)1 :
	    NULL);
	while (!waiting)
		usleep(1000);
	usleep(20000);			/* it is blocked now */
	/* What it inherits is not its own policy. */
	if (pthread_getschedparam(h, &pol, &sp) != 0) e(84);
	if (pol != SCHED_OTHER || sp.sched_priority != 0) e(85);
	if (how == GIVEUP && pthread_join(w, NULL) != 0) e(87);
	go = 1;
	hog_cpu();
	if (pthread_join(h, NULL) != 0) e(86);
	if (how != GIVEUP && pthread_join(w, NULL) != 0) e(87);
	return gap;
}

static void
test160a(void)
{
	pthread_mutex_t plain, inh;

	subtest = 1;

	make_mutex(&plain, PTHREAD_PRIO_NONE, PTHREAD_MUTEX_NORMAL);
	make_mutex(&inh, PTHREAD_PRIO_INHERIT, PTHREAD_MUTEX_NORMAL);
	set_main(SCHED_FIFO, MAIN_PRIO);

	/* Without inheritance, the spinner keeps the holder off: the test can
	 * tell. */
	if (!STARVED(held_gap(&plain, SCHED_FIFO, WAIT_PRIO, HOLD))) e(1);
	/* With it, the holder runs at the waiter's priority.  Twice: the
	 * mutex stays one once unlocked. */
	if (!RAN(held_gap(&inh, SCHED_FIFO, WAIT_PRIO, HOLD))) e(2);
	if (!RAN(held_gap(&inh, SCHED_FIFO, WAIT_PRIO, HOLD))) e(3);
	/* A time-sharing waiter has no priority to pass on. */
	if (!STARVED(held_gap(&inh, SCHED_OTHER, 0, HOLD))) e(4);
	/* The holder keeps it only while it holds the mutex, and while the
	 * waiter waits. */
	if (!STARVED(held_gap(&inh, SCHED_FIFO, WAIT_PRIO, LETGO))) e(5);
	if (!STARVED(held_gap(&inh, SCHED_FIFO, WAIT_PRIO, GIVEUP))) e(6);
	/* Recursive, the same. */
	pthread_mutex_destroy(&inh);
	make_mutex(&inh, PTHREAD_PRIO_INHERIT, PTHREAD_MUTEX_RECURSIVE);
	if (!RAN(held_gap(&inh, SCHED_FIFO, WAIT_PRIO, HOLD))) e(7);

	set_main(SCHED_OTHER, 0);
	pthread_mutex_destroy(&plain);
	pthread_mutex_destroy(&inh);
}

static void
test160b(void)
{
	pthread_mutex_t m1, m2;
	pthread_t h, mid, w;

	subtest = 2;

	/* A chain: the holder has m1, for which the middle thread waits while
	 * holding m2, for which a real-time thread waits.  The holder runs at
	 * the priority of the last. */
	make_mutex(&m1, PTHREAD_PRIO_INHERIT, PTHREAD_MUTEX_NORMAL);
	make_mutex(&m2, PTHREAD_PRIO_INHERIT, PTHREAD_MUTEX_NORMAL);
	set_main(SCHED_FIFO, MAIN_PRIO);

	held = &m1;
	ready = go = waiting = 0;
	h = start_thread(holder, SCHED_OTHER, 0, NULL);
	while (!ready)
		usleep(1000);
	mid = start_thread(middle, SCHED_OTHER, 0, &m2);
	while (!waiting)
		usleep(1000);
	usleep(20000);
	wanted = &m2;
	waiting = 0;
	w = start_thread(waiter, SCHED_FIFO, WAIT_PRIO, NULL);
	while (!waiting)
		usleep(1000);
	usleep(20000);
	hog_cpu();
	if (pthread_join(h, NULL) != 0) e(1);
	if (pthread_join(mid, NULL) != 0) e(2);
	if (pthread_join(w, NULL) != 0) e(3);
	if (!RAN(gap)) e(4);

	set_main(SCHED_OTHER, 0);
	pthread_mutex_destroy(&m1);
	pthread_mutex_destroy(&m2);
}

static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int signalled;

static void *
cond_waiter(void *arg)
{
	pthread_mutex_t *m = arg;

	if (pthread_mutex_lock(m) != 0) e(60);
	waiting = 1;
	while (!signalled)
		if (pthread_cond_wait(&cv, m) != 0) e(61);
	if (pthread_mutex_unlock(m) != 0) e(62);
	return NULL;
}

static void *
try_it(void *arg)
{
	return (void *)(long) pthread_mutex_trylock(arg);
}

static void *
lock_it(void *arg)
{
	if (pthread_mutex_lock(arg) != 0) e(63);
	if (pthread_mutex_unlock(arg) != 0) e(64);
	return NULL;
}

static void
test160c(void)
{
	pthread_mutexattr_t ma;
	pthread_mutex_t m;
	pthread_t t;
	void *r;
	int proto, ceil;

	subtest = 3;

	/* The attribute, and what goes with it. */
	if (pthread_mutexattr_init(&ma) != 0) e(1);
	if (pthread_mutexattr_setprotocol(&ma, PTHREAD_PRIO_INHERIT) != 0)
		e(2);
	if (pthread_mutexattr_getprotocol(&ma, &proto) != 0 ||
	    proto != PTHREAD_PRIO_INHERIT) e(3);
	if (pthread_mutex_init(&m, &ma) != 0) e(4);
	pthread_mutexattr_destroy(&ma);
	/* A ceiling is for PTHREAD_PRIO_PROTECT. */
	if (pthread_mutex_getprioceiling(&m, &ceil) != EINVAL) e(5);

	/* Trylock, free and held. */
	if (pthread_mutex_trylock(&m) != 0) e(6);
	if (pthread_create(&t, NULL, try_it, &m) != 0) e(7);
	if (pthread_join(t, &r) != 0 || (long) r != EBUSY) e(8);
	if (pthread_mutex_unlock(&m) != 0) e(9);
	if (pthread_mutex_lock(&m) != 0) e(10);
	if (pthread_mutex_unlock(&m) != 0) e(11);

	/* A condition variable: the waiter wakes holding the mutex. */
	waiting = signalled = 0;
	if (pthread_create(&t, NULL, cond_waiter, &m) != 0) e(12);
	while (!waiting)
		usleep(1000);
	if (pthread_mutex_lock(&m) != 0) e(13);
	signalled = 1;
	if (pthread_cond_signal(&cv) != 0) e(14);
	if (pthread_mutex_unlock(&m) != 0) e(15);
	if (pthread_join(t, NULL) != 0) e(16);

	/* One that waits, and one that gives up, at once: the first still
	 * gets it. */
	if (pthread_mutex_lock(&m) != 0) e(17);
	if (pthread_create(&t, NULL, lock_it, &m) != 0) e(18);
	wanted = &m;
	waiting = 0;
	{
		pthread_t t2;

		if (pthread_create(&t2, NULL, waiter, (void *)1) != 0) e(19);
		if (pthread_join(t2, NULL) != 0) e(20);
	}
	if (pthread_mutex_unlock(&m) != 0) e(21);
	if (pthread_join(t, NULL) != 0) e(22);
	if (pthread_mutex_trylock(&m) != 0) e(23);
	if (pthread_mutex_unlock(&m) != 0) e(24);
	pthread_mutex_destroy(&m);
}

int
main(int argc, char **argv)
{
	cpuset_t *set;
	long ncpu;

	start(160);

	/* All on one CPU, so that the spinner keeps a thread off for sure.
	 * The highest: the boot CPU runs the system services.
	 */
	if ((ncpu = sysconf(_SC_NPROCESSORS_ONLN)) < 1)
		ncpu = 1;
	if ((set = cpuset_create()) == NULL) e(1);
	cpuset_zero(set);
	if (cpuset_set(ncpu - 1, set) != 0) e(2);
	if (sched_setaffinity_np(0, cpuset_size(set), set) != 0) e(3);
	cpuset_destroy(set);

	alarm(120);
	test160a();
	test160b();
	test160c();
	alarm(0);

	quit();
	return 0;
}
