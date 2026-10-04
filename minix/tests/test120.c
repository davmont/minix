/* Test 120 - sessions and process groups: setsid(2), setpgid(2), getsid(2),
 * getpgid(2), and process-group targets of kill(2) and waitpid(2).
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

#define NOPID	0x7ffffff0

static char self[1024];

/* A child that waits until its pipe is closed, then exits with 'code'. */
static pid_t
waiter(int *wfd, int code)
{
	int p[2];
	pid_t pid;
	char c;

	if (pipe(p) != 0) e(90);
	if ((pid = fork()) < 0) e(91);
	if (pid == 0) {
		close(p[1]);
		(void)read(p[0], &c, 1);
		_exit(code);
	}
	close(p[0]);
	*wfd = p[1];
	return pid;
}

static void
reap(pid_t pid, int wfd, int code)
{
	int status;

	if (wfd >= 0) close(wfd);
	if (waitpid(pid, &status, 0) != pid) e(92);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != code) e(93);
}

/* Run 'fn' in a child; it reports failure through its exit status. */
static void
in_child(int (*fn)(void))
{
	pid_t pid;
	int status, r;

	if ((pid = fork()) < 0) e(94);
	if (pid == 0) {
		r = fn();
		_exit(r);
	}
	if (waitpid(pid, &status, 0) != pid) e(95);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		printf("child step failed: %d\n", WEXITSTATUS(status));
		e(96);
	}
}

static void
test_query(void)
{

	subtest = 1;
	if (getsid(0) != getsid(getpid())) e(1);
	if (getpgid(0) != getpgrp()) e(2);
	if (getpgid(getpid()) != getpgrp()) e(3);
	if (getpgid(NOPID) != -1 || errno != ESRCH) e(4);
	if (getsid(NOPID) != -1 || errno != ESRCH) e(5);
	if (getpgid(-1) != -1 || errno != EINVAL) e(6);
}

static int
step_setsid(void)
{
	pid_t me = getpid();

	if (setsid() != me) return 1;
	if (getsid(0) != me || getpgrp() != me) return 2;
	if (setsid() != -1 || errno != EPERM) return 3;	/* a group leader */
	/* A session leader stays where it is. */
	if (setpgid(0, 0) != -1 || errno != EPERM) return 4;
	return 0;
}

static int
step_setsid_group_named(void)
{
	pid_t me = getpid(), parent_grp = getpgrp(), q;
	int wfd, status;

	/* Lead a group, leave a child in it, go back to the old group:
	 * our pid still names a group, so setsid() must fail.
	 */
	if (setpgid(0, 0) != 0) return 1;
	q = waiter(&wfd, 0);
	if (getpgid(q) != me) return 2;
	if (setpgid(0, parent_grp) != 0) return 3;
	if (getpgrp() != parent_grp) return 4;
	if (setsid() != -1 || errno != EPERM) return 5;
	close(wfd);
	if (waitpid(q, &status, 0) != q) return 6;
	return 0;
}

static void
test_setsid(void)
{

	subtest = 2;
	in_child(step_setsid);
	in_child(step_setsid_group_named);
}

static void
test_setpgid(void)
{
	pid_t c, grp = getpgrp();
	int wfd, p[2], status;
	char c1;

	subtest = 3;
	c = waiter(&wfd, 0);
	if (setpgid(c, c) != 0) e(1);
	if (getpgid(c) != c) e(2);
	if (setpgid(c, grp) != 0) e(3);			/* and back */
	if (getpgid(c) != grp) e(4);
	if (setpgid(c, NOPID) != -1 || errno != EPERM) e(5);	/* no group */
	if (setpgid(c, -1) != -1 || errno != EINVAL) e(6);
	if (setpgid(1, 0) != -1 || errno != ESRCH) e(7);	/* not ours */
	if (setpgid(NOPID, 0) != -1 || errno != ESRCH) e(8);
	if (setpgid(0, 0) != 0 && errno != EPERM) e(9);
	reap(c, wfd, 0);

	subtest = 4;
	/* After exec, the parent may no longer move the child. */
	if (pipe(p) != 0) e(1);
	if ((c = fork()) < 0) e(2);
	if (c == 0) {
		close(p[0]);
		dup2(p[1], 3);
		execl(self, self, "exec-wait", (char *)NULL);
		_exit(1);
	}
	close(p[1]);
	if (read(p[0], &c1, 1) != 1) e(3);		/* it has exec'd */
	if (setpgid(c, c) != -1 || errno != EACCES) e(4);
	kill(c, SIGTERM);
	if (waitpid(c, &status, 0) != c) e(5);
	close(p[0]);
}

static int
step_session_rules(void)
{
	pid_t grp, c;
	int wfd, p[2];
	char ch;

	/* A child that becomes a session leader cannot be moved. */
	if (pipe(p) != 0) return 1;
	if ((c = fork()) < 0) return 2;
	if (c == 0) {
		close(p[0]);
		if (setsid() < 0) _exit(1);
		(void)write(p[1], "x", 1);
		pause();
		_exit(0);
	}
	close(p[1]);
	if (read(p[0], &ch, 1) != 1) return 3;
	if (setpgid(c, c) != -1 || errno != EPERM) return 4;
	/* Nor can we join its group, in another session. */
	if (setpgid(0, c) != -1 || errno != EPERM) return 5;
	kill(c, SIGKILL);
	waitpid(c, NULL, 0);

	/* A group of our session can be joined. */
	grp = getpgrp();
	c = waiter(&wfd, 0);
	if (setpgid(c, c) != 0) return 6;
	if (setpgid(0, c) != 0) return 7;
	if (getpgrp() != c) return 8;
	if (setpgid(0, grp) != 0) return 9;
	close(wfd);
	waitpid(c, NULL, 0);
	return 0;
}

static void
test_sessions(void)
{

	subtest = 5;
	in_child(step_session_rules);
}

static void
test_targets(void)
{
	pid_t a, b;
	int wa, wb, status;

	subtest = 6;
	/* kill(-pgid) and waitpid(-pgid) see that group only. */
	a = waiter(&wa, 1);
	b = waiter(&wb, 2);
	if (setpgid(a, a) != 0) e(1);
	if (kill(-a, SIGUSR1) != 0) e(2);
	if (waitpid(-a, &status, 0) != a) e(3);
	if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGUSR1) e(4);
	if (waitpid(-a, &status, WNOHANG) != -1 || errno != ECHILD) e(5);
	if (waitpid(b, &status, WNOHANG) != 0) e(6);	/* b lives on */
	close(wa);
	reap(b, wb, 2);
}

static void *
report_pgrp(void *arg)
{

	*(pid_t *)arg = getpgrp();
	return NULL;
}

static int
step_threads(void)
{
	pthread_t t;
	pid_t seen = -1;

	/* A thread follows its process into the new group. */
	if (setpgid(0, 0) != 0) return 1;
	if (pthread_create(&t, NULL, report_pgrp, &seen) != 0) return 2;
	if (pthread_join(t, NULL) != 0) return 3;
	if (seen != getpid()) return 4;
	return 0;
}

static void
test_spawn(void)
{
	posix_spawnattr_t sa;
	char *argv[] = { "sleep", "5", NULL };
	pid_t pid;
	int status;

	subtest = 7;
	in_child(step_threads);

	subtest = 8;
	/* posix_spawn() can put the child in its own group. */
	if (posix_spawnattr_init(&sa) != 0) e(1);
	if (posix_spawnattr_setflags(&sa, POSIX_SPAWN_SETPGROUP) != 0) e(2);
	if (posix_spawnattr_setpgroup(&sa, 0) != 0) e(3);
	if (posix_spawn(&pid, "/bin/sleep", NULL, &sa, argv, NULL) != 0) e(4);
	if (getpgid(pid) != pid) e(5);
	kill(pid, SIGKILL);
	if (waitpid(pid, &status, 0) != pid) e(6);
	posix_spawnattr_destroy(&sa);
}

int
main(int argc, char **argv)
{

	if (argc == 2 && !strcmp(argv[1], "exec-wait")) {
		(void)write(3, "x", 1);			/* exec is done */
		pause();
		exit(0);
	}

	/* start() changes directory: keep an absolute path to ourselves. */
	if (argv[0][0] == '/')
		strlcpy(self, argv[0], sizeof(self));
	else if (getcwd(self, sizeof(self)) != NULL) {
		strlcat(self, "/", sizeof(self));
		strlcat(self, argv[0], sizeof(self));
	}

	start(120);

	test_query();
	test_setsid();
	test_setpgid();
	test_sessions();
	test_targets();
	test_spawn();

	quit();
	return 0;
}
