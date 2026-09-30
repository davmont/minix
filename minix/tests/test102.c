/* Test 102 - a CPU-bound thread must not starve the others.
 *
 * A thread that spins without making system calls gives the kernel no chance
 * to switch away from it except through the timer tick and preemption.  On
 * SMP both used to fail on an application processor: a CPU woken from idle
 * could return to such a thread with its local timer left disarmed, and a
 * higher-priority thread woken there from another CPU was not preempted in.
 * The thread that created the spinner then stalled for seconds, or for good.
 *
 * Each round forks a fresh child (after a pause, so that the other CPUs go
 * idle first), which starts a pure spinner and then times a few ordinary
 * operations of its own.  None may take longer than a second.
 */
#include <sys/select.h>
#include <sys/wait.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

#include "common.h"

int max_error = 0;

#define ROUNDS		8
#define LIMIT_MS	1000

static volatile unsigned long spins;

static void *
spinner(void *arg)
{
	for (;;)
		spins++;
	return arg;
}

static void *
idler(void *arg)
{
	for (;;)
		pause();
	return arg;
}

static long
ms_since(struct timespec *t0)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (t.tv_sec - t0->tv_sec) * 1000 +
	    (t.tv_nsec - t0->tv_nsec) / 1000000;
}

/* In a child: start a spinner, then time thread creations and short sleeps.
 * Report 0 if every step was quick, or the number of the slow step, through
 * the pipe; then wait to be killed (a fatal signal ends every thread). */
static void
child(int fd)
{
	struct timespec t0;
	pthread_t t;
	char r = 0;
	int i;

	if (pthread_create(&t, NULL, spinner, NULL) != 0)
		r = 90;
	for (i = 0; i < 4 && r == 0; i++) {
		clock_gettime(CLOCK_MONOTONIC, &t0);
		if (pthread_create(&t, NULL, idler, NULL) != 0)
			r = 91;
		else if (ms_since(&t0) > LIMIT_MS)
			r = 10 + i;
		clock_gettime(CLOCK_MONOTONIC, &t0);
		usleep(10000);
		if (r == 0 && ms_since(&t0) > LIMIT_MS)
			r = 20 + i;
	}
	(void) write(fd, &r, 1);
	for (;;)
		pause();
}

static void
test_spinner(void)
{
	struct timeval tv;
	fd_set fds;
	int round, fd[2], status;
	char r;
	pid_t pid;

	subtest = 1;
	for (round = 0; round < ROUNDS; round++) {
		usleep(200000);		/* let the other CPUs go idle */
		if (pipe(fd) != 0) e(1);
		if ((pid = fork()) < 0) e(2);
		if (pid == 0) {
			close(fd[0]);
			child(fd[1]);
		}
		close(fd[1]);
		FD_ZERO(&fds);
		FD_SET(fd[0], &fds);
		tv.tv_sec = 60;
		tv.tv_usec = 0;
		if (select(fd[0] + 1, &fds, NULL, NULL, &tv) != 1) {
			e(3);			/* no report within a minute */
		} else if (read(fd[0], &r, 1) != 1) {
			e(4);
		} else if (r != 0) {
			printf("test102: round %d: step %d was slow\n", round, r);
			e(5);
		}
		close(fd[0]);
		kill(pid, SIGKILL);
		if (waitpid(pid, &status, 0) != pid) e(6);
	}
}

int
main(int argc, char **argv)
{
	start(102);

	test_spinner();

	quit();
	return 0;
}
