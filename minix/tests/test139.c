/* Test 139 - FORTIFY catches overflows of buffers of known size.
 *
 * With _FORTIFY_SOURCE=2, which user programs are built with on amd64, the
 * wrappers in <ssp/> check the size the compiler knows a destination buffer
 * to have against what a call would write into it, and abort the process
 * (SIGABRT, from __chk_fail()) when it would not fit.  Each subtest overflows
 * an 8-byte buffer in a child by a length the compiler cannot see, and
 * expects SIGABRT; the same calls within bounds must not abort.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

#define SMALL	8

/* Lengths and strings the compiler cannot fold, so that the checks happen
 * at run time. */
static volatile size_t fit = SMALL, over = 4 * SMALL;
static char longstr[4 * SMALL + 1];

static void
do_op(int op, size_t len)
{
	char buf[SMALL];
	char src[4 * SMALL];
	int fd;

	memset(src, 'x', sizeof(src));
	switch (op) {
	case 1:
		longstr[len - 1] = '\0';
		strcpy(buf, longstr);
		break;
	case 2:
		memcpy(buf, src, len);
		break;
	case 3:
		snprintf(buf, len, "%s", longstr);
		break;
	case 4:
		if ((fd = open("/dev/zero", O_RDONLY)) < 0)
			_exit(2);
		(void)read(fd, buf, len);
		break;
	}
	/* Keep buf alive. */
	_exit(buf[0] == 'z' ? 3 : 0);
}

static void
test_op(int op)
{
	pid_t pid;
	int status;

	subtest = op;

	/* Within bounds: no abort. */
	if ((pid = fork()) == 0)
		do_op(op, fit);
	if (pid < 0 || waitpid(pid, &status, 0) != pid) { e(1); return; }
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) e(2);

	/* Overflow: SIGABRT. */
	if ((pid = fork()) == 0)
		do_op(op, over);
	if (pid < 0 || waitpid(pid, &status, 0) != pid) { e(3); return; }
	if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGABRT) e(4);
}

int
main(int argc, char **argv)
{
	int op;

	start(139);

#if defined(__x86_64__)
	memset(longstr, 'y', sizeof(longstr) - 1);
	for (op = 1; op <= 4; op++)
		test_op(op);
#endif

	quit();
	return 0;
}
