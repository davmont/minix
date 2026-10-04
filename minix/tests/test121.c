/* Test 121 - job control in the process manager: stopping and continuing.
 *
 * Stop signals stop a process (all its threads) by default, SIGCONT continues
 * it, the parent learns of both through waitpid(WUNTRACED/WCONTINUED) and
 * SIGCHLD (CLD_STOPPED/CLD_CONTINUED, not with SA_NOCLDSTOP), signals wait
 * while it is stopped, SIGKILL and SIGSTOP cannot be caught or ignored, and
 * the terminal stop signals do nothing to an orphaned process group.
 *
 * The children of this test run in their own process groups: the group the
 * test suite runs in is itself orphaned, so SIGTSTP would not stop them.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

static int beat[2];		/* children write a byte to it while running */

/* Write a byte now and then until killed. */
static void
beating(void)
{

	for (;;) {
		(void)write(beat[1], "b", 1);
		usleep(20000);
	}
}

/* How many bytes arrive in 'ms' milliseconds; also drains what was there. */
static int
beats(int ms)
{
	char buf[256];
	int n, total = 0;

	usleep(ms * 1000);
	while ((n = read(beat[0], buf, sizeof(buf))) > 0)
		total += n;
	return total;
}

/* Fork a beating child in its own process group (or a new session). */
static pid_t
spawn(int new_session, void (*setup)(void))
{
	pid_t pid;

	if ((pid = fork()) < 0) e(90);
	if (pid == 0) {
		/* The shell may have left the terminal stop signals ignored,
		 * and ignored signals are inherited.
		 */
		signal(SIGTSTP, SIG_DFL);
		signal(SIGTTIN, SIG_DFL);
		signal(SIGTTOU, SIG_DFL);
		if (new_session) {
			if (setsid() < 0) _exit(1);
		} else if (setpgid(0, 0) != 0) _exit(1);
		if (setup != NULL) setup();
		beating();
	}
	if (!new_session)
		(void)setpgid(pid, pid);	/* either of us may win */
	(void)beats(150);			/* it is up */
	return pid;
}

/* A blocking waitpid() gives up after 10 s: a test failure, not a hang. */
static void
on_alarm(int sig)
{
}

static pid_t
wait_for(pid_t pid, int *status, int options)
{
	struct sigaction sa;
	pid_t r;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_alarm;		/* no SA_RESTART */
	sigaction(SIGALRM, &sa, NULL);
	alarm(10);
	r = waitpid(pid, status, options);
	alarm(0);
	return r;
}

static void
finish(pid_t pid)
{
	int status;

	kill(pid, SIGKILL);
	if (wait_for(pid, &status, 0) != pid) e(91);
	(void)beats(0);
}

static void
expect_stop(pid_t pid, int sig)
{
	int status;

	if (wait_for(pid, &status, WUNTRACED) != pid) e(92);
	if (!WIFSTOPPED(status) || WSTOPSIG(status) != sig) {
		printf("status 0x%x, expected stop by %d\n", status, sig);
		e(93);
	}
	if (WIFCONTINUED(status) || WIFEXITED(status) || WIFSIGNALED(status))
		e(94);
	(void)beats(50);
}

static void
test_stop_cont(void)
{
	pid_t c;
	int status;

	subtest = 1;
	c = spawn(0, NULL);
	if (kill(c, SIGSTOP) != 0) e(1);
	expect_stop(c, SIGSTOP);
	if (beats(300) != 0) e(2);		/* it really stopped */
	if (waitpid(c, &status, WUNTRACED | WNOHANG) != 0) e(3);  /* told */

	if (kill(c, SIGCONT) != 0) e(4);
	if (wait_for(c, &status, WCONTINUED) != c) e(5);
	if (!WIFCONTINUED(status) || WIFSTOPPED(status)) e(6);
	if (beats(300) < 3) e(7);		/* it runs again */

	/* Without WUNTRACED a stop is not reported. */
	if (kill(c, SIGSTOP) != 0) e(8);
	usleep(100000);
	if (waitpid(c, &status, WNOHANG) != 0) e(9);
	if (wait_for(c, &status, WUNTRACED) != c || !WIFSTOPPED(status)) e(10);
	kill(c, SIGCONT);
	finish(c);
}

static volatile sig_atomic_t chld, chld_code, chld_status, chld_pid;

static void
on_chld(int sig, siginfo_t *si, void *ctx)
{

	chld++;
	chld_code = si->si_code;
	chld_status = si->si_status;
	chld_pid = si->si_pid;
}

static void
catch_chld(int flags)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = on_chld;
	sa.sa_flags = SA_SIGINFO | flags;
	if (sigaction(SIGCHLD, &sa, NULL) != 0) e(95);
}

static void
wait_chld(int n)
{
	int i;

	for (i = 0; i < 100 && chld < n; i++)
		usleep(10000);
}

static void
test_sigchld(void)
{
	pid_t c;
	int status;

	subtest = 2;
	catch_chld(0);
	chld = 0;
	c = spawn(0, NULL);
	kill(c, SIGSTOP);
	wait_chld(1);
	if (chld != 1 || chld_code != CLD_STOPPED || chld_status != SIGSTOP ||
	    chld_pid != c) e(1);
	kill(c, SIGCONT);
	wait_chld(2);
	if (chld != 2 || chld_code != CLD_CONTINUED || chld_status != SIGCONT)
		e(2);
	/* The reports are still there for waitpid. */
	if (wait_for(c, &status, WCONTINUED) != c || !WIFCONTINUED(status))
		e(3);
	finish(c);

	subtest = 3;
	/* SA_NOCLDSTOP: no SIGCHLD for stop and continue, still for exit. */
	catch_chld(SA_NOCLDSTOP);
	chld = 0;
	c = spawn(0, NULL);
	kill(c, SIGSTOP);
	expect_stop(c, SIGSTOP);
	kill(c, SIGCONT);
	usleep(200000);
	if (chld != 0) e(1);
	finish(c);
	wait_chld(1);
	if (chld != 1 || chld_code != CLD_KILLED) e(2);
	signal(SIGCHLD, SIG_DFL);
}

static void
on_tstp(int sig)
{

	(void)write(beat[1], "T", 1);
}

static void
catch_tstp(void)
{

	signal(SIGTSTP, on_tstp);
}

static void
test_terminal_stops(void)
{
	pid_t c;
	int status;
	char buf[64];
	ssize_t n;

	subtest = 4;
	/* SIGTSTP, SIGTTIN and SIGTTOU stop by default. */
	c = spawn(0, NULL);
	kill(c, SIGTSTP);
	expect_stop(c, SIGTSTP);
	kill(c, SIGCONT);
	kill(c, SIGTTIN);
	expect_stop(c, SIGTTIN);
	kill(c, SIGCONT);
	kill(c, SIGTTOU);
	expect_stop(c, SIGTTOU);
	kill(c, SIGCONT);
	finish(c);

	subtest = 5;
	/* A caught SIGTSTP runs the handler and does not stop. */
	c = spawn(0, catch_tstp);
	kill(c, SIGTSTP);
	usleep(200000);
	n = read(beat[0], buf, sizeof(buf));
	if (n <= 0 || memchr(buf, 'T', n) == NULL) e(1);
	if (waitpid(c, &status, WUNTRACED | WNOHANG) != 0) e(2);
	finish(c);

	subtest = 6;
	/* An orphaned process group (a new session here) ignores SIGTSTP;
	 * SIGSTOP stops it all the same.
	 */
	c = spawn(1, NULL);
	kill(c, SIGTSTP);
	usleep(200000);
	if (waitpid(c, &status, WUNTRACED | WNOHANG) != 0) e(1);
	if (beats(200) < 3) e(2);
	kill(c, SIGSTOP);
	expect_stop(c, SIGSTOP);
	kill(c, SIGCONT);
	finish(c);
}

static void
on_usr1(int sig)
{

	(void)write(beat[1], "U", 1);
}

static void
catch_usr1(void)
{

	signal(SIGUSR1, on_usr1);
}

static void
block_tstp_for_a_while(void)
{
	sigset_t set;

	/* Block SIGTSTP for 400 ms, then take whatever is pending. */
	sigemptyset(&set);
	sigaddset(&set, SIGTSTP);
	sigprocmask(SIG_BLOCK, &set, NULL);
	usleep(400000);
	sigprocmask(SIG_UNBLOCK, &set, NULL);
}

static void
test_rules(void)
{
	struct sigaction sa;
	pid_t c;
	int status;
	char buf[64];
	ssize_t n;

	subtest = 7;
	/* SIGKILL and SIGSTOP cannot be caught or ignored. */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_usr1;
	if (sigaction(SIGSTOP, &sa, NULL) != -1 || errno != EINVAL) e(1);
	if (sigaction(SIGKILL, &sa, NULL) != -1 || errno != EINVAL) e(2);
	sa.sa_handler = SIG_IGN;
	if (sigaction(SIGSTOP, &sa, NULL) != -1 || errno != EINVAL) e(3);
	if (signal(SIGKILL, SIG_IGN) != SIG_ERR) e(4);
	sa.sa_handler = SIG_DFL;
	if (sigaction(SIGSTOP, &sa, NULL) != 0) e(5);

	subtest = 8;
	/* Signals sent while stopped wait for SIGCONT. */
	c = spawn(0, catch_usr1);
	kill(c, SIGSTOP);
	expect_stop(c, SIGSTOP);
	kill(c, SIGUSR1);
	usleep(200000);
	if (beats(0) != 0) e(1);
	kill(c, SIGCONT);
	usleep(200000);
	n = read(beat[0], buf, sizeof(buf));
	if (n <= 0 || memchr(buf, 'U', n) == NULL) e(2);
	finish(c);

	subtest = 9;
	/* SIGKILL ends a stopped process. */
	c = spawn(0, NULL);
	kill(c, SIGSTOP);
	expect_stop(c, SIGSTOP);
	kill(c, SIGKILL);
	if (waitpid(c, &status, 0) != c) e(1);
	if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL) e(2);
	(void)beats(0);

	subtest = 10;
	/* SIGCONT throws away a pending stop signal. */
	c = spawn(0, block_tstp_for_a_while);
	usleep(50000);
	kill(c, SIGTSTP);			/* pending: blocked */
	kill(c, SIGCONT);			/* discards it */
	usleep(500000);				/* it unblocks SIGTSTP */
	if (waitpid(c, &status, WUNTRACED | WNOHANG) != 0) e(1);
	if (beats(200) < 3) e(2);
	finish(c);
}

static void *
thread_beat(void *arg)
{

	beating();
	return NULL;
}

static void
start_thread(void)
{
	pthread_t t;

	if (pthread_create(&t, NULL, thread_beat, NULL) != 0) _exit(2);
}

static int in_pipe[2];

static void
echo_input(void)
{
	char c;

	/* Blocked in read(): echo every byte as it arrives. */
	for (;;) {
		if (read(in_pipe[0], &c, 1) != 1) _exit(3);
		(void)write(beat[1], "E", 1);
	}
}

static void
test_threads_and_calls(void)
{
	pid_t c;
	char buf[64];
	ssize_t n;
	int status;

	subtest = 11;
	/* All threads of the process stop. */
	c = spawn(0, start_thread);
	kill(c, SIGSTOP);
	expect_stop(c, SIGSTOP);
	if (beats(300) != 0) e(1);
	kill(c, SIGCONT);
	if (beats(300) < 6) e(2);			/* two beating */
	finish(c);

	subtest = 12;
	/* A process stopped while blocked in a system call finishes the
	 * call only once continued.
	 */
	if (pipe(in_pipe) != 0) e(1);
	if ((c = fork()) < 0) e(2);
	if (c == 0) {
		setpgid(0, 0);
		echo_input();
	}
	setpgid(c, c);
	usleep(100000);
	kill(c, SIGSTOP);
	expect_stop(c, SIGSTOP);
	if (write(in_pipe[1], "x", 1) != 1) e(3);
	if (beats(300) != 0) e(4);			/* no echo yet */
	kill(c, SIGCONT);
	usleep(200000);
	n = read(beat[0], buf, sizeof(buf));
	if (n != 1 || buf[0] != 'E') e(5);
	kill(c, SIGKILL);
	if (waitpid(c, &status, 0) != c) e(6);
	close(in_pipe[0]);
	close(in_pipe[1]);
}

int
main(int argc, char **argv)
{

	start(121);

	if (pipe(beat) != 0) e(1);
	if (fcntl(beat[0], F_SETFL, O_NONBLOCK) != 0) e(2);

	test_stop_cont();
	test_sigchld();
	test_terminal_stops();
	test_rules();
	test_threads_and_calls();

	quit();
	return 0;
}
