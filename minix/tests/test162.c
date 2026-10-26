/* Test 162 - a process goes while its threads exit.
 *
 * exit(), a fatal signal and exec take the other threads of a process down
 * before the process goes.  A thread that is in pthread_exit() already, on
 * its way out, must not hold that up: the process goes all the same, with
 * the status it should.  Each case is a child whose threads pthread_exit()
 * just as it goes; one that is not gone within 10 s has hung.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

#define ROUNDS		30
#define THREADS		3

static volatile int go;

static void *
leaver(void *arg)
{
	while (!go)
		sched_yield();
	pthread_exit(arg);
}

/* Start the threads, let them go, and after a while that is not the same
 * each round, go too.
 */
static void
threads_then(int round)
{
	pthread_t t;
	volatile int spin;
	int i;

	for (i = 0; i < THREADS; i++)
		if (pthread_create(&t, NULL, leaver, NULL) != 0)
			_exit(99);
	usleep(1000);
	go = 1;
	for (spin = 0; spin < (round % 10) * 200; spin++)
		;
}

/* The status of 'pid', or -1 if it has not gone within 10 s. */
static int
gone(pid_t pid)
{
	int status, i;

	for (i = 0; i < 1000; i++) {
		if (waitpid(pid, &status, WNOHANG) == pid)
			return status;
		usleep(10000);
	}
	kill(pid, SIGKILL);
	return -1;
}

static void
test162a(void)
{
	pid_t pid;
	int i, status;

	subtest = 1;

	/* exit() */
	for (i = 0; i < ROUNDS; i++) {
		if ((pid = fork()) == 0) {
			threads_then(i);
			exit(7);
		}
		if (pid < 0) e(1);
		status = gone(pid);
		if (status == -1) { e(2); break; }
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 7) e(3);
	}
}

static void
test162b(void)
{
	pid_t pid;
	int i, status;

	subtest = 2;

	/* A fatal signal. */
	for (i = 0; i < ROUNDS; i++) {
		if ((pid = fork()) == 0) {
			threads_then(i);
			kill(getpid(), SIGKILL);
			_exit(1);
		}
		if (pid < 0) e(1);
		status = gone(pid);
		if (status == -1) { e(2); break; }
		if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL) e(3);
	}
}

static void
test162c(void)
{
	pid_t pid;
	int i, status;

	subtest = 3;

	/* exec */
	for (i = 0; i < ROUNDS; i++) {
		if ((pid = fork()) == 0) {
			threads_then(i);
			execl("/bin/sh", "sh", "-c", "exit 5", (char *) NULL);
			_exit(1);
		}
		if (pid < 0) e(1);
		status = gone(pid);
		if (status == -1) { e(2); break; }
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 5) e(3);
	}
}

int
main(int argc, char **argv)
{
	start(162);

	alarm(300);
	test162a();
	test162b();
	test162c();
	alarm(0);

	quit();
	return 0;
}
