/* Test 111 - setresuid(2), setresgid(2), getresuid(2), getresgid(2).
 *
 * Each case runs in a child, so that the test keeps its own ids.  The test
 * runs setuid root (see "run"), so the effective uid is 0 to begin with.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

static int
in_child(void (*fn)(void))
{
	int status;
	pid_t pid;

	if ((pid = fork()) < 0) return -1;
	if (pid == 0) {
		fn();
		_exit(0);
	}
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status)) return -1;
	return WEXITSTATUS(status);
}

static int
ids_are(uid_t r, uid_t e, uid_t s)
{
	uid_t gr, ge, gs;

	if (getresuid(&gr, &ge, &gs) != 0) return 0;
	return gr == r && ge == e && gs == s && getuid() == r && geteuid() == e;
}

static int
gids_are(gid_t r, gid_t e, gid_t s)
{
	gid_t gr, ge, gs;

	if (getresgid(&gr, &ge, &gs) != 0) return 0;
	return gr == r && ge == e && gs == s && getgid() == r && getegid() == e;
}

static void
child_uids(void)
{
	/* Privileged: anything goes. */
	if (setresuid(10, 20, 30) != 0) _exit(1);
	if (!ids_are(10, 20, 30)) _exit(2);

	/* Unprivileged now (effective 20): only ids we have. */
	if (setresuid(30, -1, -1) != 0) _exit(3);		/* the saved one */
	if (!ids_are(30, 20, 30)) _exit(4);
	if (setresuid(-1, 40, -1) != -1 || errno != EPERM) _exit(5);
	if (setresuid(-1, -1, 10) != -1 || errno != EPERM) _exit(6);	/* gone */
	/* All or nothing. */
	if (setresuid(20, 99, -1) != -1 || errno != EPERM) _exit(7);
	if (!ids_are(30, 20, 30)) _exit(8);
	if (setresuid(-1, 30, 20) != 0) _exit(9);
	if (!ids_are(30, 30, 20)) _exit(10);
	if (setresuid(-1, -1, -1) != 0) _exit(11);
	if (!ids_are(30, 30, 20)) _exit(12);
	/* No way back to root. */
	if (setresuid(0, 0, 0) != -1 || errno != EPERM) _exit(13);
	if (setuid(0) != -1) _exit(14);
}

static void
child_gids(void)
{
	if (setresgid(5, 6, 7) != 0) _exit(1);
	if (!gids_are(5, 6, 7)) _exit(2);
	if (setresuid(50, 50, 50) != 0) _exit(3);	/* drop root */
	if (setresgid(7, -1, 5) != 0) _exit(4);
	if (!gids_are(7, 6, 5)) _exit(5);
	if (setresgid(-1, 8, -1) != -1 || errno != EPERM) _exit(6);
	if (!gids_are(7, 6, 5)) _exit(7);
}

/* VFS checks access with the new effective uid. */
static void
child_vfs(void)
{
	if (setresuid(-1, 60, -1) != 0) _exit(1);	/* root kept as real */
	if (open("rootfile", O_RDONLY) != -1 || errno != EACCES) _exit(2);
	if (setresuid(-1, 0, -1) != 0) _exit(3);	/* back, via real 0 */
	if (open("rootfile", O_RDONLY) < 0) _exit(4);
}

int
main(int argc, char **argv)
{
	uid_t r, e, s;
	gid_t gr, ge, gs;
	int fd, res;

	start(111);

	subtest = 1;
	if (getresuid(&r, &e, &s) != 0) e(1);
	if (r != getuid() || e != geteuid()) e(2);
	if (getresgid(&gr, &ge, &gs) != 0) e(3);
	if (gr != getgid() || ge != getegid()) e(4);
	if (getresuid(NULL, NULL, NULL) != 0) e(5);
	if (geteuid() != 0) {
		/* The rest needs root. */
		quit();
		return 0;
	}

	subtest = 2;
	if ((res = in_child(child_uids)) != 0) e(10 + res);

	subtest = 3;
	if ((res = in_child(child_gids)) != 0) e(10 + res);

	subtest = 4;
	if (setuid(0) != 0) e(1);		/* real root too */
	if ((fd = open("rootfile", O_WRONLY | O_CREAT, 0600)) < 0) e(2);
	close(fd);
	if ((res = in_child(child_vfs)) != 0) e(10 + res);

	quit();
	return 0;
}
