/* Test 122 - waitid(2): choosing children (P_PID, P_PGID, P_ALL) and what to
 * hear of (WEXITED, WSTOPPED, WCONTINUED), WNOWAIT and WNOHANG, and the
 * siginfo_t it fills in, both at once and after waiting.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

/* A wait that never returns is a failure: say where, instead of hanging. */
static void
watchdog(int sig)
{

	printf("timed out in subtest %d\n", subtest);
	fflush(stdout);
	_exit(1);
}

/* A child in its own process group that exits with 'code' after 'ms'. */
static pid_t
child(int code, int ms, int own_group)
{
	pid_t pid;

	if ((pid = fork()) < 0) e(90);
	if (pid == 0) {
		signal(SIGTSTP, SIG_DFL);
		if (own_group) setpgid(0, 0);
		if (ms > 0) usleep(ms * 1000);
		if (code < 0)
			for (;;) pause();
		_exit(code);
	}
	if (own_group) setpgid(pid, pid);
	return pid;
}

static void
check(const siginfo_t *si, pid_t pid, int code, int status)
{

	if (si->si_signo != SIGCHLD || si->si_pid != pid ||
	    si->si_code != code || si->si_status != status) {
		printf("siginfo: signo %d pid %d code %d status %d; "
		    "expected pid %d code %d status %d\n", si->si_signo,
		    (int)si->si_pid, si->si_code, si->si_status, (int)pid,
		    code, status);
		e(91);
	}
	if (si->si_uid != getuid()) e(92);
}

static void
test_exit(void)
{
	siginfo_t si;
	pid_t c;
	int status;

	subtest = 1;
	c = child(7, 0, 0);
	if (waitid(P_PID, c, &si, WEXITED) != 0) e(1);
	check(&si, c, CLD_EXITED, 7);
	if (waitpid(c, &status, WNOHANG) != -1 || errno != ECHILD) e(2);

	subtest = 2;
	/* WNOWAIT: reported, and still there to be waited for. */
	c = child(3, 0, 0);
	if (waitid(P_PID, c, &si, WEXITED | WNOWAIT) != 0) e(1);
	check(&si, c, CLD_EXITED, 3);
	if (waitid(P_PID, c, &si, WEXITED | WNOWAIT) != 0) e(2);
	check(&si, c, CLD_EXITED, 3);
	if (waitpid(c, &status, 0) != c || WEXITSTATUS(status) != 3) e(3);

	subtest = 3;
	/* Killed by a signal. */
	c = child(-1, 0, 0);
	kill(c, SIGTERM);
	if (waitid(P_PID, c, &si, WEXITED) != 0) e(1);
	check(&si, c, CLD_KILLED, SIGTERM);
}

static void
test_stop(void)
{
	siginfo_t si;
	pid_t c;

	subtest = 4;
	c = child(-1, 0, 1);
	kill(c, SIGSTOP);
	if (waitid(P_PID, c, &si, WSTOPPED) != 0) e(1);
	check(&si, c, CLD_STOPPED, SIGSTOP);
	/* Reported once without WNOWAIT. */
	if (waitid(P_PID, c, &si, WSTOPPED | WNOHANG) != 0) e(2);
	if (si.si_pid != 0) e(3);

	kill(c, SIGCONT);
	if (waitid(P_PID, c, &si, WCONTINUED | WNOWAIT) != 0) e(4);
	check(&si, c, CLD_CONTINUED, SIGCONT);
	if (waitid(P_PID, c, &si, WCONTINUED) != 0) e(5);	/* still there */
	check(&si, c, CLD_CONTINUED, SIGCONT);
	/* WEXITED alone does not report stops. */
	kill(c, SIGSTOP);
	usleep(100000);
	if (waitid(P_PID, c, &si, WEXITED | WNOHANG) != 0) e(6);
	if (si.si_pid != 0) e(7);
	kill(c, SIGKILL);
	if (waitid(P_PID, c, &si, WEXITED) != 0) e(8);
	check(&si, c, CLD_KILLED, SIGKILL);
}

static void
test_select(void)
{
	siginfo_t si;
	pid_t a, b, d;

	subtest = 5;
	/* WNOHANG with nothing to report: success, si_pid 0. */
	a = child(-1, 0, 1);
	memset(&si, 0xff, sizeof(si));
	if (waitid(P_PID, a, &si, WEXITED | WNOHANG) != 0) e(1);
	if (si.si_pid != 0) e(2);

	subtest = 6;
	/* P_PGID picks that group's children only. */
	b = child(5, 300, 0);			/* alive while we move it */
	if (setpgid(b, a) != 0) e(1);		/* b joins a's group */
	d = child(6, 0, 0);			/* in our group */
	usleep(200000);
	if (waitid(P_PGID, a, &si, WEXITED) != 0) e(2);
	check(&si, b, CLD_EXITED, 5);
	if (waitid(P_PGID, a, &si, WEXITED | WNOHANG) != 0) e(3);
	if (si.si_pid != 0) e(4);		/* a lives on */
	/* P_PGID 0: our own group. */
	if (waitid(P_PGID, 0, &si, WEXITED) != 0) e(5);
	check(&si, d, CLD_EXITED, 6);
	kill(a, SIGKILL);
	if (waitid(P_ALL, 0, &si, WEXITED) != 0) e(6);
	check(&si, a, CLD_KILLED, SIGKILL);

	subtest = 7;
	if (waitid(P_ALL, 0, &si, 0) != -1 || errno != EINVAL) e(1);
	if (waitid(P_ALL, 0, &si, WEXITED | 0x1000) != -1 || errno != EINVAL)
		e(2);
	if (waitid((idtype_t)99, 0, &si, WEXITED) != -1 || errno != EINVAL)
		e(3);
	if (waitid(P_ALL, 0, &si, WEXITED) != -1 || errno != ECHILD) e(4);
	if (waitid(P_PID, 1, &si, WEXITED) != -1 || errno != ECHILD) e(5);
}

static void
test_blocking(void)
{
	siginfo_t si;
	pid_t c;
	int status;

	subtest = 8;
	/* Waiting first: the exit comes later. */
	c = child(9, 200, 0);
	if (waitid(P_PID, c, &si, WEXITED) != 0) e(1);
	check(&si, c, CLD_EXITED, 9);

	/* The same with WNOWAIT: the child is left. */
	c = child(4, 200, 0);
	if (waitid(P_ALL, 0, &si, WEXITED | WNOWAIT) != 0) e(2);
	check(&si, c, CLD_EXITED, 4);
	if (waitpid(c, &status, 0) != c || WEXITSTATUS(status) != 4) e(3);

	subtest = 9;
	/* A stop that comes while we wait for it. */
	if ((c = fork()) < 0) e(1);
	if (c == 0) {
		setpgid(0, 0);
		usleep(200000);
		kill(getpid(), SIGSTOP);
		_exit(0);
	}
	setpgid(c, c);
	if (waitid(P_PID, c, &si, WSTOPPED) != 0) e(2);
	check(&si, c, CLD_STOPPED, SIGSTOP);
	kill(c, SIGCONT);
	if (waitid(P_PID, c, &si, WEXITED) != 0) e(3);
	check(&si, c, CLD_EXITED, 0);
}

int
main(int argc, char **argv)
{

	start(122);

	signal(SIGALRM, watchdog);
	alarm(20);
	test_exit();
	alarm(20);
	test_stop();
	alarm(20);
	test_select();
	alarm(20);
	test_blocking();
	alarm(0);

	quit();
	return 0;
}
