/* Test 132 - children belong to the process, not to the thread that forked.
 *
 * POSIX: any thread of a process may wait for any of its children, a child
 * forked by a thread has the process as its parent (getppid), and outlives
 * the thread.  Threads are separate processes to PM; this checks that they
 * are treated as one for waiting.  Also, a signal sent to the process goes to
 * a thread that does not block it, when the main thread does.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
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

static int sigpipe_fd[2];
static volatile pthread_t caught_by;

static void
on_sig(int sig)
{

	caught_by = pthread_self();
}

static void *
sig_taker(void *arg)
{
	sigset_t set;
	char c;

	/* Take the signals that the main thread blocks; wait in a read. */
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	sigaddset(&set, SIGALRM);
	if (pthread_sigmask(SIG_UNBLOCK, &set, NULL) != 0) e(1);
	while (read(sigpipe_fd[0], &c, 1) != 1)
		;
	return NULL;
}

static void
wait_caught(pthread_t t, int n)
{
	int i;

	for (i = 0; i < 300 && caught_by == NULL; i++)
		usleep(10000);
	if (caught_by == NULL || !pthread_equal(caught_by, t)) e(n);
}

static void
test_process_signal(void)
{
	struct sigaction sa;
	sigset_t set;
	pthread_t t;

	subtest = 4;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_sig;
	if (sigaction(SIGUSR1, &sa, NULL) != 0) e(1);
	if (sigaction(SIGALRM, &sa, NULL) != 0) e(2);
	if (pipe(sigpipe_fd) != 0) e(3);

	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	sigaddset(&set, SIGALRM);
	if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0) e(4);
	if (pthread_create(&t, NULL, sig_taker, NULL) != 0) e(5);
	usleep(200000);			/* let it unblock them and wait */

	/* kill(2) to the process, and an alarm. */
	caught_by = NULL;
	if (kill(getpid(), SIGUSR1) != 0) e(6);
	wait_caught(t, 7);
	caught_by = NULL;
	alarm(1);
	wait_caught(t, 8);

	if (write(sigpipe_fd[1], "x", 1) != 1) e(9);
	if (pthread_join(t, NULL) != 0) e(10);
	if (pthread_sigmask(SIG_UNBLOCK, &set, NULL) != 0) e(11);
	close(sigpipe_fd[0]);
	close(sigpipe_fd[1]);
}

static void *
reader(void *arg)
{
	sigset_t set;
	char c;

	sigemptyset(&set);
	pthread_sigmask(SIG_SETMASK, &set, NULL);
	while (read(sigpipe_fd[0], &c, 1) != 0)
		;
	return NULL;
}

static void
on_usr1(int sig)
{
}

/* A caught signal makes PM ask VFS to unpause the thread taking it, here one
 * waiting in a read while the main thread blocks all signals; a fatal signal
 * right after tears that thread down while the unpause is going on.
 */
static void
test_signal_teardown(void)
{
	struct sigaction sa;
	sigset_t set;
	pthread_t t;
	int i, j, status, sig;
	pid_t pid;

	subtest = 5;
	for (i = 0; i < 40; i++) {
		sig = (i & 1) ? SIGKILL : SIGTERM;
		if (pipe(sigpipe_fd) != 0) e(1);
		if ((pid = fork()) == 0) {
			memset(&sa, 0, sizeof(sa));
			sa.sa_handler = on_usr1;
			sigaction(SIGUSR1, &sa, NULL);
			sigfillset(&set);
			pthread_sigmask(SIG_SETMASK, &set, NULL);
			for (j = 0; j < 4; j++)
				pthread_create(&t, NULL, reader, NULL);
			pthread_join(t, NULL);
			_exit(0);
		}
		if (pid < 0) e(2);
		usleep(200000);			/* threads waiting */
		if (kill(pid, SIGUSR1) != 0) e(3);
		if (kill(pid, sig) != 0) e(4);
		alarm(10);
		if (waitpid(pid, &status, 0) != pid) {
			e(5);
			(void)kill(pid, SIGKILL);
			(void)waitpid(pid, &status, 0);
		}
		alarm(0);
		if (!WIFSIGNALED(status) || WTERMSIG(status) != sig) e(6);
		close(sigpipe_fd[0]);
		close(sigpipe_fd[1]);
	}
}

int
main(int argc, char **argv)
{

	start(132);

	test_wait_from_thread();
	test_fork_from_thread();
	test_stop_report();
	test_process_signal();
	test_signal_teardown();

	quit();
	return 0;
}
