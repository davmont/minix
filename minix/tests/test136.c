/* Test 136 - the stack protector.
 *
 * User programs and libraries are built with -fstack-protector.  A
 * function that overruns a local buffer must not return through the
 * clobbered frame: the guard check kills the process with SIGABRT.  The
 * guard has to be unpredictable for that to stop an attack, so libc takes
 * it from the random driver at startup: not the fixed fallback canary, and
 * not the same in two programs.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

extern long __stack_chk_guard[];

/* The canary libc falls back to when there is no entropy: 00 00 '\n' ff. */
static int
fallback_canary(long g)
{
	unsigned char *p = (unsigned char *)&g;

	return p[0] == 0 && p[1] == 0 && p[2] == '\n' && p[3] == 255;
}

/* Overrun a 16-byte local buffer by 'len' bytes.  The length is volatile
 * so that the compiler can neither see nor refuse the overflow.
 */
static volatile size_t overrun = 64;

static void __attribute__((__noinline__))
smash(void)
{
	char buf[16];

	memset(buf, 'A', sizeof(buf) + overrun);
	__asm__ __volatile__("" : : "r" (buf) : "memory");
}

static void
test_smash(void)
{
	pid_t pid;
	int status;

	subtest = 1;
	if ((pid = fork()) == 0) {
		/* No core file in the test directory. */
		signal(SIGABRT, SIG_DFL);
		smash();
		_exit(0);	/* the check did not fire */
	}
	if (pid < 0) e(1);
	if (waitpid(pid, &status, 0) != pid) e(2);
	if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGABRT) {
		printf("smashed child: status 0x%x\n", status);
		e(3);
	}
}

/* The guard of a freshly exec'd copy of this program. */
static long
child_guard(const char *self)
{
	char buf[64];
	ssize_t r;
	pid_t pid;
	int fds[2], status;

	if (pipe(fds) != 0) e(10);
	if ((pid = fork()) == 0) {
		close(fds[0]);
		if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(1);
		execl(self, self, "guard", (char *)NULL);
		_exit(1);
	}
	if (pid < 0) e(11);
	close(fds[1]);
	r = read(fds[0], buf, sizeof(buf) - 1);
	close(fds[0]);
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0 || r <= 0) {
		e(12);
		return 0;
	}
	buf[r] = '\0';
	return (long)strtoul(buf, NULL, 16);
}

static void
test_guard(const char *self)
{
	long g1, g2;

	subtest = 2;
	if (__stack_chk_guard[0] == 0) e(1);
	if (fallback_canary(__stack_chk_guard[0])) e(2);

	g1 = child_guard(self);
	g2 = child_guard(self);
	if (g1 == 0 || fallback_canary(g1) || fallback_canary(g2)) e(3);
	/* 2^-64 to fail by chance. */
	if (g1 == g2 || g1 == __stack_chk_guard[0]) e(4);
}

int
main(int argc, char **argv)
{
	char self[PATH_MAX];

	if (argc == 2 && strcmp(argv[1], "guard") == 0) {
		printf("%lx\n", (unsigned long)__stack_chk_guard[0]);
		return 0;
	}
	/* start() moves to a directory of its own. */
	if (realpath(argv[0], self) == NULL) {
		printf("realpath %s: %s\n", argv[0], strerror(errno));
		return 1;
	}

	start(136);

	test_smash();
	test_guard(self);

	quit();
	return 0;
}
