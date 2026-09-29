/* Test 98 - sigaltstack(2) and SA_ONSTACK.
 *
 * An SA_ONSTACK handler runs on the alternate signal stack, sees SS_ONSTACK
 * there and may not change the stack; argument errors are caught; and --
 * the reason alternate stacks exist -- a process that overflows its stack
 * can still catch the SIGSEGV and recover.
 */
#include <setjmp.h>
#include <signal.h>

#include "common.h"

int max_error = 0;

static char altstack[4 * MINSIGSTKSZ];
static volatile int got, on_alt, saw_onstack, change_errno;
static sigjmp_buf jb;

static int
on_altstack(void *p)
{
	return (char *)p > altstack && (char *)p <= altstack + sizeof(altstack);
}

static void
handler(int sig)
{
	char here;
	stack_t ss, oss;

	got = sig;
	on_alt = on_altstack(&here);
	if (sigaltstack(NULL, &oss) == 0)
		saw_onstack = !!(oss.ss_flags & SS_ONSTACK);
	/* Changing the stack we are running on is refused. */
	ss.ss_sp = altstack;
	ss.ss_size = sizeof(altstack);
	ss.ss_flags = 0;
	change_errno = (sigaltstack(&ss, NULL) == -1) ? errno : 0;
}

static void
install(int sig, void (*fn)(int), int flags)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = flags;
	sa.sa_handler = fn;
	if (sigaction(sig, &sa, NULL) != 0) e(90);
}

static void
set_altstack(void)
{
	stack_t ss;

	ss.ss_sp = altstack;
	ss.ss_size = sizeof(altstack);
	ss.ss_flags = 0;
	if (sigaltstack(&ss, NULL) != 0) e(91);
}

/* The handler runs on the alternate stack, and knows it. */
static void
test_onstack(void)
{
	stack_t oss;

	subtest = 1;
	if (sigaltstack(NULL, &oss) != 0) e(1);
	if (!(oss.ss_flags & SS_DISABLE)) e(2);	/* none to start with */

	set_altstack();
	if (sigaltstack(NULL, &oss) != 0) e(3);
	if (oss.ss_sp != altstack || oss.ss_size != sizeof(altstack)) e(4);
	if (oss.ss_flags & (SS_DISABLE | SS_ONSTACK)) e(5);

	install(SIGUSR1, handler, SA_ONSTACK);
	got = on_alt = saw_onstack = change_errno = 0;
	if (kill(getpid(), SIGUSR1) != 0) e(6);
	if (got != SIGUSR1) e(7);
	if (!on_alt) e(8);
	if (!saw_onstack) e(9);
	if (change_errno != EPERM) e(10);

	/* Without SA_ONSTACK the handler stays on the normal stack. */
	install(SIGUSR2, handler, 0);
	got = on_alt = 0;
	if (kill(getpid(), SIGUSR2) != 0) e(11);
	if (got != SIGUSR2 || on_alt) e(12);
}

/* Argument checking, and turning the alternate stack off. */
static void
test_errors(void)
{
	stack_t ss;

	subtest = 2;
	ss.ss_sp = altstack;
	ss.ss_size = MINSIGSTKSZ - 1;
	ss.ss_flags = 0;
	if (sigaltstack(&ss, NULL) != -1 || errno != ENOMEM) e(1);
	ss.ss_size = sizeof(altstack);
	ss.ss_flags = 0x100;
	if (sigaltstack(&ss, NULL) != -1 || errno != EINVAL) e(2);

	ss.ss_flags = SS_DISABLE;
	if (sigaltstack(&ss, NULL) != 0) e(3);
	install(SIGUSR1, handler, SA_ONSTACK);
	got = on_alt = 0;
	if (kill(getpid(), SIGUSR1) != 0) e(4);
	if (got != SIGUSR1 || on_alt) e(5);	/* no alternate stack now */
}

static volatile int sink;

static int
recurse(int depth)
{
	volatile char frame[1024];

	frame[0] = (char)depth;
	return recurse(depth + 1) + frame[0];
}

static void
overflow_handler(int sig)
{
	char here;

	got = sig;
	on_alt = on_altstack(&here);
	siglongjmp(jb, 1);
}

/* Overflow the stack: the SIGSEGV can only be delivered on the alternate
 * stack, and the process carries on. */
static void
test_overflow(void)
{
	subtest = 3;
	set_altstack();
	install(SIGSEGV, overflow_handler, SA_ONSTACK);
	got = on_alt = 0;
	if (sigsetjmp(jb, 1) == 0)
		sink = recurse(0);
	if (got != SIGSEGV) e(1);
	if (!on_alt) e(2);
	signal(SIGSEGV, SIG_DFL);
}

int
main(int argc, char **argv)
{
	start(98);

	test_onstack();
	test_errors();
	test_overflow();

	quit();
	return 0;
}
