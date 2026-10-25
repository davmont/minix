/* Test 155 - the nice level of a threaded process.
 *
 * The nice level is the process's (POSIX).  Each thread is a slot of its own
 * in PM, and setpriority() and nice() changed only one: the leader's, for a
 * pid, or the calling thread's.  The other threads kept running at the old
 * level, and a thread saw a level of its own.
 *
 * Here threads wait while the main thread, and then one of them, change the
 * process's nice level; every thread must see it, and so must threads
 * created afterwards and a child that a thread forks.
 */
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

#define THREADS	4

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static int round_no;		/* advanced by the main thread */
static int seen[THREADS];	/* the level each thread saw, per round */
static int done[THREADS];

static int
level(void)
{
	errno = 0;
	return getpriority(PRIO_PROCESS, 0);
}

static void *
waiter(void *arg)
{
	int id = (int)(long)arg, r;

	for (r = 1; r <= 2; r++) {
		pthread_mutex_lock(&lock);
		while (round_no < r)
			pthread_cond_wait(&cond, &lock);
		seen[id] = level();
		done[id] = r;
		pthread_cond_broadcast(&cond);
		pthread_mutex_unlock(&lock);
	}
	return NULL;
}

static void *
renicer(void *arg)
{
	if (setpriority(PRIO_PROCESS, 0, 7) != 0)
		return arg;
	return NULL;
}

static void *
forker(void *arg)
{
	pid_t pid;
	int status;

	if ((pid = fork()) == 0)
		_exit(level() == 7 ? 0 : 1);
	if (pid < 0 || waitpid(pid, &status, 0) != pid ||
	    !WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return arg;
	return NULL;
}

/* Let the waiters look at the level, round 'r', and check what they saw. */
static void
look(int r, int want, int n)
{
	int i;

	pthread_mutex_lock(&lock);
	round_no = r;
	pthread_cond_broadcast(&cond);
	for (i = 0; i < THREADS; i++) {
		while (done[i] < r)
			pthread_cond_wait(&cond, &lock);
		if (seen[i] != want) e(n);
	}
	pthread_mutex_unlock(&lock);
}

static void
test155a(void)
{
	pthread_t t[THREADS], u;
	void *res;
	int i, base;

	subtest = 1;

	base = level();
	if (base == -1 && errno != 0) e(1);
	if (base > 5) {			/* already reniced: nothing to see */
		return;
	}
	for (i = 0; i < THREADS; i++)
		if (pthread_create(&t[i], NULL, waiter, (void *)(long)i) != 0)
			e(2);

	/* The main thread sets it, by pid. */
	if (setpriority(PRIO_PROCESS, getpid(), 5) != 0) e(3);
	if (level() != 5) e(4);
	if (getpriority(PRIO_PROCESS, getpid()) != 5) e(5);
	look(1, 5, 6);

	/* Another thread sets it, as itself. */
	if (pthread_create(&u, NULL, renicer, NULL) != 0) e(7);
	if (pthread_join(u, &res) != 0 || res != NULL) e(8);
	if (level() != 7) e(9);
	if (getpriority(PRIO_PROCESS, getpid()) != 7) e(10);
	look(2, 7, 11);

	for (i = 0; i < THREADS; i++)
		if (pthread_join(t[i], NULL) != 0) e(12);

	/* A new thread has it, and a child that a thread forks. */
	if (pthread_create(&u, NULL, forker, NULL) != 0) e(13);
	if (pthread_join(u, &res) != 0 || res != NULL) e(14);
}

int
main(int argc, char **argv)
{
	pid_t pid;
	int status;

	start(155);

	/* In a child, so that this process keeps its level. */
	if ((pid = fork()) == 0) {
		test155a();
		exit(errct);
	}
	if (pid < 0) e(100);
	else if (waitpid(pid, &status, 0) != pid) e(101);
	else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) e(102);

	quit();
	return 0;
}
