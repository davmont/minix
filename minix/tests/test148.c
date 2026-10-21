/* Test 148 - a process that exits with a thread in a timed wait.
 *
 * A thread blocked in pthread_cond_timedwait() is parked in PM with a timer.
 * When its process exited (or was killed) the thread went away without the
 * timer being cancelled, so it stayed in PM's timer list after the slot was
 * freed; the next process or thread created in that slot copied its creator's
 * slot over it, and PM crashed on the corrupted list when timers next ran,
 * taking the system down.
 *
 * Here children leave threads in timed waits by exiting and by being killed;
 * then many short-lived processes reuse the slots before and after the
 * timeouts would have passed.  If PM dies, the test does not finish.
 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

#define NWAITERS	8
#define TIMEOUT_MS	1500

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;

static void *
waiter(void *arg)
{
	struct timespec ts;

	(void)arg;
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += TIMEOUT_MS / 1000;
	ts.tv_nsec += (TIMEOUT_MS % 1000) * 1000000L;
	if (ts.tv_nsec >= 1000000000L) {
		ts.tv_sec++;
		ts.tv_nsec -= 1000000000L;
	}
	pthread_mutex_lock(&mtx);
	for (;;)
		(void)pthread_cond_timedwait(&cv, &mtx, &ts);
	/* NOTREACHED */
	return NULL;
}

/* A child whose threads are in timed waits when it goes. */
static pid_t
spawn_waiters(int killed)
{
	pthread_t t;
	pid_t pid;
	int i;

	if ((pid = fork()) == 0) {
		for (i = 0; i < 2; i++)
			if (pthread_create(&t, NULL, waiter, NULL) != 0)
				_exit(1);
		usleep(100000);			/* they are parked now */
		if (killed)
			pause();
		_exit(0);
	}
	if (pid < 0) e(1);
	if (killed) {
		usleep(200000);
		if (kill(pid, SIGKILL) != 0) e(2);
	}
	return pid;
}

/* Reuse process slots: many short-lived processes. */
static void
churn(int n)
{
	pid_t pid;
	int i, status;

	for (i = 0; i < n; i++) {
		if ((pid = fork()) == 0)
			_exit(0);
		if (pid < 0) { e(3); return; }
		if (waitpid(pid, &status, 0) != pid || status != 0) e(4);
	}
}

int
main(int argc, char **argv)
{
	pid_t pids[NWAITERS];
	int i, status;

	start(148);

	for (i = 0; i < NWAITERS; i++)
		pids[i] = spawn_waiters(i & 1);
	for (i = 0; i < NWAITERS; i++)
		if (waitpid(pids[i], &status, 0) != pids[i]) e(5);

	/* Before the timeouts, and after them. */
	churn(100);
	usleep(TIMEOUT_MS * 1000 * 2);
	churn(100);

	quit();
	return 0;
}
