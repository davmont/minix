/* Test 103 - process-wide signal state of a threaded process.
 *
 * pthread_exit() in main() leaves the process running until its last
 * thread exits; signals sent to the process then go to a live thread.  And
 * a signal handler is the process's, whichever thread installed it.  Each
 * case runs in a child; the parent checks how it ended, with a timeout.
 */
#include <sys/wait.h>
#include <pthread.h>
#include <signal.h>

#include "common.h"

int max_error = 0;

static volatile int caught;

static void
handler(int sig)
{
	caught = sig;
}

/* Run fn in a child; return its wait status, or -1 if it had to be killed
 * after a minute.  'sig' (if non-zero) is sent to it after 'sig_ms'. */
static int
run_child(void (*fn)(void), int sig, int sig_ms)
{
	int status, i;
	pid_t pid, r;

	if ((pid = fork()) < 0) e(80);
	if (pid == 0) {
		fn();
		_exit(99);
	}
	if (sig != 0) {
		usleep(sig_ms * 1000);
		kill(pid, sig);
	}
	for (i = 0; i < 6000; i++) {
		r = waitpid(pid, &status, WNOHANG);
		if (r == pid)
			return status;
		if (r < 0) e(81);
		usleep(10000);
	}
	kill(pid, SIGKILL);
	(void) waitpid(pid, &status, 0);
	return -1;
}

static void *
worker_returns(void *arg)
{
	usleep(100000);
	if (getpid() <= 0) _exit(20);	/* the process is still there */
	return arg;			/* the last thread: process exits, 0 */
}

static void *
worker_exits(void *arg)
{
	usleep(100000);
	exit(7);
}

static void *
worker_waits_signal(void *arg)
{
	int i;

	for (i = 0; i < 300 && caught == 0; i++)
		usleep(10000);
	exit(caught == SIGUSR1 ? 9 : 21);
}

static void *
worker_forever(void *arg)
{
	for (;;)
		pause();
	return arg;
}

static void
main_leaves(void *(*worker)(void *))
{
	pthread_t t;
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = handler;
	if (sigaction(SIGUSR1, &sa, NULL) != 0) _exit(90);
	if (pthread_create(&t, NULL, worker, NULL) != 0) _exit(91);
	pthread_exit(NULL);		/* main goes; the process must not */
}

static void child_returns(void) { main_leaves(worker_returns); }
static void child_exits(void) { main_leaves(worker_exits); }
static void child_signal(void) { main_leaves(worker_waits_signal); }
static void child_killed(void) { main_leaves(worker_forever); }

/* pthread_exit() in main() */
static void
test_main_exits(void)
{
	int s;

	subtest = 1;			/* last thread returns: status 0 */
	s = run_child(child_returns, 0, 0);
	if (s == -1) e(1);
	else if (!WIFEXITED(s) || WEXITSTATUS(s) != 0) e(2);

	subtest = 2;			/* a thread calls exit() */
	s = run_child(child_exits, 0, 0);
	if (s == -1) e(1);
	else if (!WIFEXITED(s) || WEXITSTATUS(s) != 7) e(2);

	subtest = 3;			/* a caught signal reaches a thread */
	s = run_child(child_signal, SIGUSR1, 200);
	if (s == -1) e(1);
	else if (!WIFEXITED(s) || WEXITSTATUS(s) != 9) e(2);

	subtest = 4;			/* a fatal signal ends the process */
	s = run_child(child_killed, SIGTERM, 200);
	if (s == -1) e(1);
	else if (!WIFSIGNALED(s) || WTERMSIG(s) != SIGTERM) e(2);
}

static volatile int installed;

static void *
installer(void *arg)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = handler;
	if (sigaction(SIGUSR2, &sa, NULL) != 0) _exit(92);
	installed = 1;
	for (;;)
		pause();
	return arg;
}

/* A handler another thread installs catches a signal sent to the
 * process (which the main thread's slot takes). */
static void
child_other_installs(void)
{
	pthread_t t;
	int i;

	if (pthread_create(&t, NULL, installer, NULL) != 0) _exit(90);
	while (!installed)
		usleep(1000);
	caught = 0;
	kill(getpid(), SIGUSR2);
	for (i = 0; i < 300 && caught == 0; i++)
		usleep(10000);
	exit(caught == SIGUSR2 ? 0 : 22);
}

static void
test_process_wide_sigaction(void)
{
	int s;

	subtest = 5;
	s = run_child(child_other_installs, 0, 0);
	if (s == -1) e(1);
	else if (!WIFEXITED(s) || WEXITSTATUS(s) != 0) e(2);
}

int
main(int argc, char **argv)
{
	start(103);

	test_main_exits();
	test_process_wide_sigaction();

	quit();
	return 0;
}
