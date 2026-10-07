/* Test 132 - children belong to the process, not to the thread that forked.
 *
 * POSIX: any thread of a process may wait for any of its children, a child
 * forked by a thread has the process as its parent (getppid), and outlives
 * the thread.  Threads are separate processes to PM; this checks that they
 * are treated as one for waiting.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

static pid_t child;
static int child_status;
static pid_t forked;

static void *
waiter(void *arg)
{
	int flags = (int)(long)arg;

	if (waitpid(child, &child_status, flags) != child)
		child_status = -errno - 1000;
	return NULL;
}

static void *
forker(void *arg)
{

	if ((forked = fork()) == 0) {
		usleep(300000);			/* outlive the thread */
		_exit(getppid() == (pid_t)(long)arg ? 7 : 8);
	}
	return NULL;
}

static void *
nochild(void *arg)
{

	if (waitpid(-1, NULL, WNOHANG) != -1 || errno != ECHILD)
		child_status = -1;
	else
		child_status = 0;
	return NULL;
}

static void
test_wait_from_thread(void)
{
	pthread_t t;

	subtest = 1;
	/* A child forked by the main thread, waited for by another thread,
	 * which blocks until the child exits.
	 */
	if ((child = fork()) == 0) {
		usleep(200000);
		_exit(5);
	}
	if (child < 0) e(1);
	child_status = 0;
	if (pthread_create(&t, NULL, waiter, (void *)0L) != 0) e(2);
	if (pthread_join(t, NULL) != 0) e(3);
	if (!WIFEXITED(child_status) || WEXITSTATUS(child_status) != 5) {
		printf("status %d\n", child_status);
		e(4);
	}

	/* Already a zombie when the thread waits. */
	if ((child = fork()) == 0)
		_exit(6);
	usleep(200000);
	if (pthread_create(&t, NULL, waiter, (void *)0L) != 0) e(5);
	if (pthread_join(t, NULL) != 0) e(6);
	if (!WIFEXITED(child_status) || WEXITSTATUS(child_status) != 6) e(7);

	/* No children at all, asked from a thread. */
	child_status = 1;
	if (pthread_create(&t, NULL, nochild, NULL) != 0) e(8);
	if (pthread_join(t, NULL) != 0) e(9);
	if (child_status != 0) e(10);
}

static void
test_fork_from_thread(void)
{
	pthread_t t;
	int status;

	subtest = 2;
	/* A child forked by a thread is the process's: the main thread waits
	 * for it after the thread is gone, and its parent pid is ours.
	 */
	if (pthread_create(&t, NULL, forker, (void *)(long)getpid()) != 0)
		e(1);
	if (pthread_join(t, NULL) != 0) e(2);
	if (forked <= 0) e(3);
	if (waitpid(forked, &status, 0) != forked) e(4);
	if (!WIFEXITED(status)) e(5);
	if (WEXITSTATUS(status) != 7) {
		printf("child's parent pid was not ours\n");
		e(6);
	}
}

static void
test_stop_report(void)
{
	pthread_t t;

	subtest = 3;
	/* A stop is reported to a thread waiting with WUNTRACED. */
	if ((child = fork()) == 0) {
		for (;;)
			pause();
	}
	child_status = 0;
	if (pthread_create(&t, NULL, waiter, (void *)(long)WUNTRACED) != 0)
		e(1);
	usleep(200000);
	if (kill(child, SIGSTOP) != 0) e(2);
	if (pthread_join(t, NULL) != 0) e(3);
	if (!WIFSTOPPED(child_status) || WSTOPSIG(child_status) != SIGSTOP) {
		printf("status %d\n", child_status);
		e(4);
	}
	(void)kill(child, SIGKILL);
	if (waitpid(child, NULL, 0) != child) e(5);
}

int
main(int argc, char **argv)
{

	start(132);

	test_wait_from_thread();
	test_fork_from_thread();
	test_stop_report();

	quit();
	return 0;
}
