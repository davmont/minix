/* Test 113 - ptrace(2) moves whole machine words.
 *
 * T_GETDATA/T_SETDATA and T_GETUSER/T_SETUSER transfer a long: on amd64 all
 * 64 bits come back, a write touches exactly one word, and a stored -1 is
 * told apart from an error through errno.  T_SETUSER may change the program
 * counter, but not the segment registers or the pc into the kernel half.
 */
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <minix/config.h>
#include <minix/const.h>
#include <minix/type.h>
#include <minix/ipc.h>
#include <machine/stackframe.h>
#include <machine/archtypes.h>
#include <minix/timers.h>
#include "kernel/proc.h"

#include "common.h"

int max_error = 3;

#define W0	((unsigned long)0x0123456789abcdefULL)
#define W1	((unsigned long)0xfedcba9876543210ULL)
#define W2	((unsigned long)0x5a5a5a5aa5a5a5a5ULL)
#define NEW	((unsigned long)0x1122334455667788ULL)

static volatile unsigned long words[3] = { W0, W1, W2 };

/* A stack for landed(), so that it starts with the alignment of a call. */
static long landing_stack[1024] __aligned(16);

#define PREG(f)	(offsetof(struct proc, p_reg) + offsetof(struct stackframe_s, f))

static void
landed(void)
{

	_exit(42);
}

/* Fork a child that asks to be traced and stops itself. */
static pid_t
start_child(void)
{
	pid_t pid;
	int status;

	if ((pid = fork()) < 0) e(90);
	if (pid == 0) {
		if (ptrace(T_OK, 0, 0, 0) != 0) _exit(1);
		kill(getpid(), SIGSTOP);
		_exit(0);
	}
	if (waitpid(pid, &status, 0) != pid) e(91);
	if (!WIFSTOPPED(status) || WSTOPSIG(status) != SIGSTOP) e(92);
	return pid;
}

static void
finish_child(pid_t pid, int code)
{
	int status;

	if (ptrace(T_RESUME, pid, 0, 0) != 0) e(93);
	if (waitpid(pid, &status, 0) != pid) e(94);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != code) {
		printf("child status 0x%x, expected exit %d\n", status, code);
		e(95);
	}
}

static void
test_data(void)
{
	pid_t pid;
	long v;

	subtest = 1;
	pid = start_child();

	errno = 0;
	v = ptrace(T_GETDATA, pid, (void *)&words[1], 0);
	if (errno != 0 || (unsigned long)v != W1) e(1);
	v = ptrace(T_GETINS, pid, (void *)&words[0], 0);
	if (errno != 0 || (unsigned long)v != W0) e(2);

	/* A write replaces one word, and only that one. */
	if (ptrace(T_SETDATA, pid, (void *)&words[1], (long)NEW) != 0) e(3);
	v = ptrace(T_GETDATA, pid, (void *)&words[1], 0);
	if (errno != 0 || (unsigned long)v != NEW) e(4);
	v = ptrace(T_GETDATA, pid, (void *)&words[0], 0);
	if (errno != 0 || (unsigned long)v != W0) e(5);
	v = ptrace(T_GETDATA, pid, (void *)&words[2], 0);
	if (errno != 0 || (unsigned long)v != W2) e(6);

	/* A stored -1 is a value, not an error. */
	if (ptrace(T_SETDATA, pid, (void *)&words[1], -1L) != 0) e(7);
	errno = 0;
	v = ptrace(T_GETDATA, pid, (void *)&words[1], 0);
	if (v != -1L || errno != 0) e(8);

	/* Our own copy is untouched. */
	if (words[1] != W1) e(9);

	errno = 0;
	if (ptrace(T_GETDATA, pid, NULL, 0) != -1 || errno == 0) e(10);

	finish_child(pid, 0);
}

static void
test_user(void)
{
	pid_t pid;
	long v;
	size_t off;
	int magic;

	subtest = 2;
	pid = start_child();

	/* Full words from the process table, read at long alignment as
	 * trace(1) does: the magic number lies anywhere in its word.
	 */
	off = offsetof(struct proc, p_magic);
	errno = 0;
	v = ptrace(T_GETUSER, pid, (void *)(off & ~(sizeof(long) - 1)), 0);
	if (errno != 0) e(1);
	memcpy(&magic, (char *)&v + (off & (sizeof(long) - 1)), sizeof(magic));
	if (magic != PMAGIC) e(2);
	errno = 0;
	v = ptrace(T_GETUSER, pid, (void *)PREG(pc), 0);
	if (errno != 0 || v == 0) e(3);
	if (ptrace(T_GETUSER, pid, (void *)1, 0) != -1 || errno != EFAULT)
		e(11);

#if defined(__i386__) || defined(__x86_64__)
	/* The segment registers cannot be changed. */
	if (ptrace(T_SETUSER, pid, (void *)PREG(cs), 0) != -1 ||
	    errno != EFAULT) e(4);
	if (ptrace(T_SETUSER, pid, (void *)PREG(ss), 0) != -1 ||
	    errno != EFAULT) e(5);
#endif
#if defined(__x86_64__)
	if (ptrace(T_SETUSER, pid, (void *)0, 0) != -1 || errno != EFAULT)
		e(6);
	/* A pc in the kernel half, or non-canonical, is refused. */
	if (ptrace(T_SETUSER, pid, (void *)PREG(pc),
	    (long)0xffff800000001000ULL) != -1 || errno != EFAULT) e(7);
	if (ptrace(T_SETUSER, pid, (void *)PREG(pc),
	    (long)0x0000800000000000ULL) != -1 || errno != EFAULT) e(8);
#endif

#if defined(__i386__) || defined(__x86_64__)
	/* A new pc and sp take effect: the child resumes in landed(), on a
	 * stack laid out as if landed() had been called.
	 */
	if (ptrace(T_SETUSER, pid, (void *)PREG(sp), (long)&landing_stack[1024 -
	    1]) != 0) e(12);
	if (ptrace(T_SETUSER, pid, (void *)PREG(pc), (long)landed) != 0)
		e(9);
	errno = 0;
	v = ptrace(T_GETUSER, pid, (void *)PREG(pc), 0);
	if (errno != 0 || v != (long)landed) e(10);
	finish_child(pid, 42);
#else
	finish_child(pid, 0);
#endif
}

/* End to end: trace(1) reads the process table through T_GETUSER. */
static void
test_trace(void)
{
	char line[256];
	FILE *fp;
	int found;

	subtest = 3;
	if (access("/usr/bin/trace", X_OK) != 0)
		return;
	if (system("/usr/bin/trace -o trace.out /bin/echo traced "
	    ">trace.err 2>&1") != 0) {
		(void)system("cat trace.err trace.out");
		e(1);
	}
	if ((fp = fopen("trace.out", "r")) == NULL) e(2);
	found = 0;
	while (fp != NULL && fgets(line, sizeof(line), fp) != NULL)
		if (strstr(line, "write(1, ") != NULL &&
		    strstr(line, "traced") != NULL && strstr(line, "= 7") != NULL)
			found = 1;
	if (fp != NULL) fclose(fp);
	if (!found) e(3);
}

int
main(int argc, char **argv)
{

	start(113);

	test_data();
	test_user();
	test_trace();

	quit();
	return 0;
}
