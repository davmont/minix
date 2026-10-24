/* Test 152 - POSIX scheduling policies: SCHED_FIFO and SCHED_RR.
 *
 * The sched_*() calls and their errors, who may set what (a real-time policy
 * takes root), inheritance by fork() and by new threads, the pthread_attr
 * scheduling attributes, the order processes run in on one CPU (preemption
 * by a higher priority, none by an equal one under SCHED_FIFO, sched_yield()
 * and the SCHED_RR quantum), the throttling that keeps a busy real-time
 * process from starving the rest of the system, and the order real-time
 * threads get a rwlock in.
 *
 * The program is setuid root (see 'run'); a child drops to the real uid to
 * see what an ordinary user may not do.
 */
#include <sys/types.h>
#include <sys/time.h>
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

int max_error = 4;

#define RT_MAX	31	/* priorities of SCHED_FIFO and SCHED_RR: 0..31 */

static int
policy_of(pid_t pid)
{
	return sched_getscheduler(pid);
}

static int
prio_of(pid_t pid)
{
	struct sched_param sp;

	if (sched_getparam(pid, &sp) != 0)
		return -1;
	return sp.sched_priority;
}

static int
set(pid_t pid, int policy, int prio)
{
	struct sched_param sp;

	memset(&sp, 0, sizeof(sp));
	sp.sched_priority = prio;
	return sched_setscheduler(pid, policy, &sp);
}

static long
ms_since(const struct timespec *t0)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (t.tv_sec - t0->tv_sec) * 1000 +
	    (t.tv_nsec - t0->tv_nsec) / 1000000;
}

/* Use the CPU for about 'ms' milliseconds, making no system call: each one
 * would put this process at the tail of its queue (see test152d).  The loop
 * is timed once, at the start.
 */
static volatile unsigned long sink;
static unsigned long loops_per_ms;

static void
loop(unsigned long n)
{
	while (n-- > 0)
		sink++;
}

static void
calibrate(void)
{
	struct timespec t0;
	unsigned long n;
	long ms;

	for (n = 1 << 16; ; n *= 2) {
		clock_gettime(CLOCK_MONOTONIC, &t0);
		loop(n);
		if ((ms = ms_since(&t0)) >= 200)
			break;
	}
	loops_per_ms = n / ms;
}

static void
spin(long ms)
{
	loop(loops_per_ms * ms);
}

/* A child starts with no errors of its own. */
static pid_t
fork_child(void)
{
	pid_t pid;

	if ((pid = fork()) == 0)
		errct = 0;
	return pid;
}

/* A pid that is no process: that of a child already waited for. */
static pid_t
dead_pid(void)
{
	pid_t pid;

	if ((pid = fork_child()) == 0)
		_exit(0);
	if (pid < 0 || waitpid(pid, NULL, 0) != pid)
		e(900);
	return pid;
}

static void
wait_child(pid_t pid, int n)
{
	int status;

	if (waitpid(pid, &status, 0) != pid)
		e(n);
	else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		e(n + 1);
}

/* The calls, their values and their errors. */
static void
test152a(void)
{
	struct sched_param sp;
	struct timespec ts;
	pid_t dead;

	subtest = 1;

	if (sched_get_priority_min(SCHED_FIFO) != 0) e(1);
	if (sched_get_priority_max(SCHED_FIFO) != RT_MAX) e(2);
	if (sched_get_priority_min(SCHED_RR) != 0) e(3);
	if (sched_get_priority_max(SCHED_RR) != RT_MAX) e(4);
	if (sched_get_priority_min(SCHED_OTHER) != 0) e(5);
	if (sched_get_priority_max(SCHED_OTHER) != 0) e(6);
	errno = 0;
	if (sched_get_priority_max(42) != -1 || errno != EINVAL) e(7);
	if (sysconf(_SC_PRIORITY_SCHEDULING) <= 0) e(8);
	if (sysconf(_SC_THREAD_PRIORITY_SCHEDULING) <= 0) e(9);

	if (policy_of(0) != SCHED_OTHER) e(10);
	if (prio_of(0) != 0) e(11);
	if (policy_of(getpid()) != SCHED_OTHER) e(12);

	/* setscheduler returns the previous policy. */
	if (set(0, SCHED_FIFO, 10) != SCHED_OTHER) e(13);
	if (policy_of(0) != SCHED_FIFO) e(14);
	if (prio_of(getpid()) != 10) e(15);
	sp.sched_priority = 20;
	if (sched_setparam(0, &sp) != 0) e(16);
	if (policy_of(0) != SCHED_FIFO || prio_of(0) != 20) e(17);
	if (set(0, SCHED_RR, RT_MAX) != SCHED_FIFO) e(18);
	if (sched_rr_get_interval(0, &ts) != 0) e(19);
	if (ts.tv_sec != 0 || ts.tv_nsec <= 0) e(20);

	/* Bad values change nothing. */
	errno = 0;
	if (set(0, SCHED_RR, RT_MAX + 1) != -1 || errno != EINVAL) e(21);
	errno = 0;
	if (set(0, SCHED_FIFO, -1) != -1 || errno != EINVAL) e(22);
	errno = 0;
	if (set(0, 42, 0) != -1 || errno != EINVAL) e(23);
	errno = 0;
	if (sched_setscheduler(0, SCHED_FIFO, NULL) != -1 || errno != EINVAL)
		e(24);
	if (policy_of(0) != SCHED_RR || prio_of(0) != RT_MAX) e(25);

	/* SCHED_OTHER has priority 0 only. */
	errno = 0;
	if (set(0, SCHED_OTHER, 1) != -1 || errno != EINVAL) e(26);
	if (set(0, SCHED_OTHER, 0) != SCHED_RR) e(27);
	sp.sched_priority = 1;
	errno = 0;
	if (sched_setparam(0, &sp) != -1 || errno != EINVAL) e(28);
	if (policy_of(0) != SCHED_OTHER || prio_of(0) != 0) e(29);
	if (sched_rr_get_interval(0, &ts) != 0) e(30);

	/* Who is not there, and who cannot be. */
	dead = dead_pid();
	errno = 0;
	if (policy_of(dead) != -1 || errno != ESRCH) e(31);
	errno = 0;
	if (sched_getparam(dead, &sp) != -1 || errno != ESRCH) e(32);
	errno = 0;
	if (set(dead, SCHED_OTHER, 0) != -1 || errno != ESRCH) e(33);
	errno = 0;
	if (policy_of(-1) != -1 || errno != EINVAL) e(34);
	errno = 0;
	if (sched_getparam(0, NULL) != -1 || errno != EINVAL) e(35);
}

/* Permissions: a real-time policy takes root, changing another's what
 * kill() takes.
 */
static void
test152b(void)
{
	pid_t pid;

	subtest = 2;

	if ((pid = fork_child()) == 0) {
		if (setuid(getuid()) != 0) e(1);	/* drop root */
		if (geteuid() == 0) {			/* run as root */
			errno = 0;
			exit(errct);
		}
		errno = 0;
		if (set(0, SCHED_FIFO, 1) != -1 || errno != EPERM) e(2);
		errno = 0;
		if (set(0, SCHED_RR, 0) != -1 || errno != EPERM) e(3);
		if (policy_of(0) != SCHED_OTHER) e(4);
		if (set(0, SCHED_OTHER, 0) != SCHED_OTHER) e(5);
		/* Anyone may look, not change. */
		if (policy_of(1) != SCHED_OTHER) e(6);
		errno = 0;
		if (set(1, SCHED_OTHER, 0) != -1 || errno != EPERM) e(7);
		exit(errct);
	}
	if (pid < 0) e(8);
	wait_child(pid, 9);

	/* An ordinary user may go back to SCHED_OTHER. */
	if ((pid = fork_child()) == 0) {
		if (set(0, SCHED_RR, 3) != SCHED_OTHER) e(11);
		if (setuid(getuid()) != 0) e(12);
		errno = 0;
		if (geteuid() != 0 && (set(0, SCHED_FIFO, 3) != -1 ||
		    errno != EPERM)) e(13);
		if (set(0, SCHED_OTHER, 0) != SCHED_RR) e(14);
		exit(errct);
	}
	if (pid < 0) e(15);
	wait_child(pid, 16);
}

static void *
report(void *arg)
{
	int *res = arg;
	struct sched_param sp;

	if (pthread_getschedparam(pthread_self(), &res[0], &sp) != 0)
		res[0] = -1;
	res[1] = sp.sched_priority;
	res[2] = sched_getscheduler(0);		/* the thread itself */
	return NULL;
}

static void *
park(void *arg)
{

	usleep(2000);
	return arg;
}

static void
run_thread(pthread_attr_t *attr, int *res, int n)
{
	pthread_t t;

	res[0] = res[1] = res[2] = -2;
	if (pthread_create(&t, attr, report, res) != 0) e(n);
	else if (pthread_join(t, NULL) != 0) e(n + 1);
}

/* Inheritance by fork() and threads; the pthread scheduling attributes. */
static void
test152c(void)
{
	pthread_attr_t attr;
	struct sched_param sp;
	pthread_t t;
	int res[3], policy, inherit, i, bad = 0;
	pid_t pid;

	subtest = 3;

	if (set(0, SCHED_RR, 5) != SCHED_OTHER) e(1);
	if ((pid = fork_child()) == 0) {
		if (policy_of(0) != SCHED_RR || prio_of(0) != 5) e(2);
		exit(errct);
	}
	if (pid < 0) e(3);
	wait_child(pid, 4);

	/* A thread has its creator's, by default. */
	run_thread(NULL, res, 5);
	if (res[0] != SCHED_RR || res[1] != 5 || res[2] != SCHED_RR) e(7);

	/* Or those of its attributes. */
	if (pthread_attr_init(&attr) != 0) e(8);
	if (pthread_attr_getinheritsched(&attr, &inherit) != 0 ||
	    inherit != PTHREAD_INHERIT_SCHED) e(9);
	if (pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED) != 0)
		e(10);
	if (pthread_attr_setschedpolicy(&attr, SCHED_FIFO) != 0) e(11);
	sp.sched_priority = 7;
	if (pthread_attr_setschedparam(&attr, &sp) != 0) e(12);
	memset(&sp, 0, sizeof(sp));
	if (pthread_attr_getschedparam(&attr, &sp) != 0 ||
	    sp.sched_priority != 7) e(13);
	if (pthread_attr_getschedpolicy(&attr, &policy) != 0 ||
	    policy != SCHED_FIFO) e(14);
	run_thread(&attr, res, 15);
	if (res[0] != SCHED_FIFO || res[1] != 7 || res[2] != SCHED_FIFO)
		e(17);
	/* Every time: PM had a new thread long before its scheduler did, and
	 * the policy got lost in between, in about 1 in 40.
	 */
	for (i = 0; i < 300; i++) {
		run_thread(&attr, res, 15);
		if (res[0] != SCHED_FIFO || res[1] != 7) bad++;
		if (pthread_create(&t, NULL, park, NULL) != 0) e(36);
		sp.sched_priority = 3;
		if (pthread_setschedparam(t, SCHED_RR, &sp) != 0) bad++;
		if (pthread_join(t, NULL) != 0) e(37);
	}
	if (bad != 0) e(38);
	sp.sched_priority = RT_MAX + 1;
	if (pthread_attr_setschedparam(&attr, &sp) != EINVAL) e(18);
	pthread_attr_destroy(&attr);

	/* Explicit with none set is SCHED_OTHER. */
	if (pthread_attr_init(&attr) != 0) e(19);
	if (pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED) != 0)
		e(20);
	run_thread(&attr, res, 21);
	if (res[0] != SCHED_OTHER || res[1] != 0) e(23);
	pthread_attr_destroy(&attr);

	/* Per thread: changing one leaves the others. */
	if (pthread_getschedparam(pthread_self(), &policy, &sp) != 0) e(24);
	if (policy != SCHED_RR || sp.sched_priority != 5) e(25);
	sp.sched_priority = 9;
	if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0)
		e(26);
	if (policy_of(0) != SCHED_FIFO || prio_of(0) != 9) e(27);
	if (pthread_setschedprio(pthread_self(), 11) != 0) e(28);
	if (pthread_getschedparam(pthread_self(), &policy, &sp) != 0) e(29);
	if (policy != SCHED_FIFO || sp.sched_priority != 11) e(30);
	sp.sched_priority = RT_MAX + 1;
	if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != EINVAL)
		e(31);

	if (set(0, SCHED_OTHER, 0) != SCHED_FIFO) e(32);
	run_thread(NULL, res, 33);
	if (res[0] != SCHED_OTHER || res[1] != 0) e(35);
}

/* The order of execution, on one CPU: what each process writes to the
 * 'order' pipe says when it ran.  A child either waits on 'go' for the
 * parent or sleeps 'nap' ms, to become runnable while the parent spins.
 *
 * Every system call is a message to a server here, and its caller goes to
 * the tail of its queue when the reply comes: so the parent writes its mark
 * after the point where the order shows.
 */
static int go[2], order[2];

static pid_t
start_child(int policy, int prio, char mark, long nap)
{
	pid_t pid;
	char c;

	if ((pid = fork_child()) != 0) {
		/* Of equal priority, it would not run while this one spins:
		 * let it get to its nap.
		 */
		if (nap > 0)
			usleep(20000);
		return pid;
	}
	if (set(0, policy, prio) < 0) e(100);
	if (nap > 0)
		usleep(nap * 1000);
	else if (read(go[0], &c, 1) != 1) e(101);
	if (write(order[1], &mark, 1) != 1) e(102);
	exit(errct);
}

static void
check_order(const char *want, int n)
{
	char got[8];
	ssize_t r, len = strlen(want);

	memset(got, 0, sizeof(got));
	r = read(order[0], got, len);
	if (r != len || memcmp(got, want, len) != 0) {
		printf("order %.*s, want %s\n", (int)(r > 0 ? r : 0), got,
		    want);
		e(n);
	}
}

static void
mark(char c)
{
	if (write(order[1], &c, 1) != 1) e(103);
}

static void
test152d(void)
{
	pid_t pid;

	subtest = 4;

	if (sysconf(_SC_NPROCESSORS_ONLN) != 1)
		return;		/* processes may run on other CPUs */

	if (pipe(go) != 0 || pipe(order) != 0) e(1);
	if (set(0, SCHED_FIFO, 10) != SCHED_OTHER) e(2);

	/* A higher priority preempts as soon as it can run. */
	pid = start_child(SCHED_FIFO, 20, 'C', 0);
	usleep(100000);			/* until it waits */
	if (write(go[1], "x", 1) != 1) e(3);
	mark('P');
	check_order("CP", 4);
	wait_child(pid, 5);

	pid = start_child(SCHED_FIFO, 20, 'C', 100);
	spin(300);
	mark('P');
	check_order("CP", 6);
	wait_child(pid, 7);

	/* An equal one does not, under SCHED_FIFO. */
	pid = start_child(SCHED_FIFO, 10, 'C', 100);
	spin(300);
	mark('P');
	check_order("PC", 8);
	wait_child(pid, 9);

	/* sched_yield() lets it run. */
	pid = start_child(SCHED_FIFO, 10, 'C', 100);
	spin(300);
	sched_yield();
	mark('P');
	check_order("CP", 10);
	wait_child(pid, 11);

	/* A lower one waits until this one blocks. */
	pid = start_child(SCHED_FIFO, 1, 'C', 0);
	usleep(100000);
	if (write(go[1], "x", 1) != 1) e(12);
	spin(200);
	mark('P');
	sched_yield();			/* to the tail of a queue of its own */
	mark('Q');
	usleep(100000);
	check_order("PQC", 13);
	wait_child(pid, 14);

	/* Under SCHED_RR an equal one gets the CPU at the end of the
	 * quantum.
	 */
	if (set(0, SCHED_RR, 10) != SCHED_FIFO) e(15);
	pid = start_child(SCHED_RR, 10, 'C', 100);
	spin(500);			/* several quanta */
	mark('P');
	check_order("CP", 16);
	wait_child(pid, 17);

	if (set(0, SCHED_OTHER, 0) != SCHED_RR) e(18);
	close(go[0]); close(go[1]); close(order[0]); close(order[1]);
}

/* A real-time process that never blocks still leaves the CPU to others: the
 * scheduler throttles it.  Time-sharing this process waits for its child to
 * spin, then has it killed; if throttling failed this process would not run
 * again before the child's alarm.
 */
static void
test152e(void)
{
	struct timespec t0;
	long ms;
	pid_t pid;
	int status;

	subtest = 5;

	if ((pid = fork_child()) == 0) {
		alarm(20);		/* in case this process cannot run */
		if (set(0, SCHED_FIFO, RT_MAX) != SCHED_OTHER) e(1);
		for (;;)
			;
	}
	if (pid < 0) e(2);

	clock_gettime(CLOCK_MONOTONIC, &t0);
	usleep(500000);
	spin(300);		/* time-sharing, against the spinner */
	if (policy_of(pid) != SCHED_FIFO) e(3);	/* though throttled */
	usleep(500000);
	ms = ms_since(&t0);
	if (kill(pid, SIGKILL) != 0) e(4);
	if (waitpid(pid, &status, 0) != pid) e(5);
	if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL) e(6);
	/* On one CPU this process had only what the throttling left it,
	 * about 5% of the CPU: enough to get here before the alarm.
	 */
	if (ms >= 20000) e(7);
}

/* Rwlocks go to real-time threads in priority order, to a writer first at
 * the same priority, and a reader passes blocked writers of lower priority.
 */
static pthread_rwlock_t rw;
static pthread_mutex_t order_lock = PTHREAD_MUTEX_INITIALIZER;
static int rw_order[8], rw_n;
static volatile int rw_entered[8];

struct rw_arg {
	int id, write;
};

static void *
rw_thread(void *arg)
{
	struct rw_arg *a = arg;
	int r;

	rw_entered[a->id] = 1;
	r = a->write ? pthread_rwlock_wrlock(&rw) : pthread_rwlock_rdlock(&rw);
	if (r != 0)
		return arg;
	pthread_mutex_lock(&order_lock);
	rw_order[rw_n++] = a->id;
	pthread_mutex_unlock(&order_lock);
	usleep(20000);
	if (pthread_rwlock_unlock(&rw) != 0)
		return arg;
	return NULL;
}

/* Start thread 'id' at 'prio', and give it time to block on the lock. */
static void
rw_start(pthread_t *t, struct rw_arg *a, int id, int write, int prio, int n)
{
	pthread_attr_t attr;
	struct sched_param sp;

	a->id = id;
	a->write = write;
	pthread_attr_init(&attr);
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
	memset(&sp, 0, sizeof(sp));
	sp.sched_priority = prio;
	pthread_attr_setschedparam(&attr, &sp);
	if (pthread_create(t, &attr, rw_thread, a) != 0) e(n);
	pthread_attr_destroy(&attr);
	while (!rw_entered[id])
		usleep(1000);
	usleep(100000);
}

static void
rw_join(pthread_t *t, int count, int n)
{
	void *res;
	int i;

	for (i = 0; i < count; i++)
		if (pthread_join(t[i], &res) != 0 || res != NULL) e(n);
}

static void
test152f(void)
{
	struct rw_arg a[4];
	pthread_t t[4];
	pid_t pid;

	subtest = 6;

	if ((pid = fork_child()) == 0) {
		alarm(30);
		if (set(0, SCHED_FIFO, RT_MAX) != SCHED_OTHER) e(1);
		if (pthread_rwlock_init(&rw, NULL) != 0) e(2);

		/* Read held; a writer at 2 waits.  A reader at 5 gets it
		 * at once, one at 1 waits behind the writer.
		 */
		if (pthread_rwlock_rdlock(&rw) != 0) e(3);
		rw_start(&t[0], &a[0], 0, 1, 2, 4);
		if (rw_n != 0) e(5);
		rw_start(&t[1], &a[1], 1, 0, 5, 6);
		if (rw_n != 1 || rw_order[0] != 1) e(7);
		rw_start(&t[2], &a[2], 2, 0, 1, 8);
		if (rw_n != 1) e(9);
		if (pthread_rwlock_destroy(&rw) != EBUSY) e(10);
		if (pthread_rwlock_unlock(&rw) != 0) e(11);
		rw_join(t, 3, 12);
		if (rw_n != 3 || rw_order[1] != 0 || rw_order[2] != 2) e(13);

		/* Write held; a writer and a reader at 5, a writer at 2, a
		 * reader at 7: they get it in the order 7, 5 (the writer
		 * first), 5, 2.
		 */
		memset((void *)rw_entered, 0, sizeof(rw_entered));
		rw_n = 0;
		if (pthread_rwlock_wrlock(&rw) != 0) e(14);
		rw_start(&t[0], &a[0], 0, 1, 5, 15);
		rw_start(&t[1], &a[1], 1, 0, 5, 16);
		rw_start(&t[2], &a[2], 2, 1, 2, 17);
		rw_start(&t[3], &a[3], 3, 0, 7, 18);
		if (rw_n != 0) e(19);
		if (pthread_rwlock_unlock(&rw) != 0) e(20);
		rw_join(t, 4, 21);
		if (rw_n != 4 || rw_order[0] != 3 || rw_order[1] != 0 ||
		    rw_order[2] != 1 || rw_order[3] != 2) e(22);
		if (pthread_rwlock_destroy(&rw) != 0) e(23);
		exit(errct);
	}
	if (pid < 0) e(24);
	wait_child(pid, 25);
}

int
main(int argc, char **argv)
{

	start(152);
	calibrate();

	test152a();
	test152b();
	test152c();
	test152d();
	test152e();
	test152f();

	quit();
	return 0;
}
