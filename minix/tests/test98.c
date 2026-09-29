/* Test 98 - sigaltstack(2) and SA_ONSTACK.
 *
 * An SA_ONSTACK handler runs on the alternate signal stack, sees SS_ONSTACK
 * there and may not change the stack; argument errors are caught; and --
 * the reason alternate stacks exist -- a process that overflows its stack
 * can still catch the SIGSEGV and recover.
 */
#include <sys/time.h>
#include <setjmp.h>
#include <signal.h>
#include <ucontext.h>

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

/* SS_ONSTACK is accepted (and ignored) on input, as on NetBSD. */
static void
test_onstack_flag(void)
{
	stack_t ss, oss;

	subtest = 4;
	ss.ss_sp = altstack;
	ss.ss_size = sizeof(altstack);
	ss.ss_flags = SS_ONSTACK;
	if (sigaltstack(&ss, NULL) != 0) e(1);
	if (sigaltstack(NULL, &oss) != 0) e(2);
	if (oss.ss_sp != altstack || (oss.ss_flags & (SS_DISABLE|SS_ONSTACK)))
		e(3);
}

#ifdef SA_SIGINFO
static volatile int inner_flags = -1, outer_flags = -1;
static void * volatile outer_sp;

static void
inner_handler(int sig, siginfo_t *si, void *ctx)
{
	inner_flags = ((ucontext_t *)ctx)->uc_stack.ss_flags;
}

static void
outer_handler(int sig, siginfo_t *si, void *ctx)
{
	ucontext_t *uc = ctx;

	outer_flags = uc->uc_stack.ss_flags;
	outer_sp = uc->uc_stack.ss_sp;
	kill(getpid(), SIGUSR2);	/* interrupted while on the alt stack */
}

/* The ucontext's uc_stack describes the alternate stack as of the
 * interrupted context: SS_ONSTACK only for a signal taken on it. */
static void
test_uc_stack(void)
{
	struct sigaction sa;

	subtest = 5;
	set_altstack();
	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sa.sa_sigaction = inner_handler;
	if (sigaction(SIGUSR2, &sa, NULL) != 0) e(1);
	sa.sa_sigaction = outer_handler;
	if (sigaction(SIGUSR1, &sa, NULL) != 0) e(2);
	if (kill(getpid(), SIGUSR1) != 0) e(3);
	if (outer_sp != altstack) e(4);
	if (outer_flags != 0) e(5);		/* came from the normal stack */
	if (inner_flags != SS_ONSTACK) e(6);
	signal(SIGUSR1, SIG_DFL);
	signal(SIGUSR2, SIG_DFL);
}
#endif

#if defined(__x86_64__)
/* int t98_redzone(volatile int *flag): fill the 128-byte red zone below
 * %rsp with a pattern, spin until *flag is set (by a signal handler), and
 * return how many of the 16 words changed. */
int t98_redzone(volatile int *flag);
__asm__(
	".text\n"
	".globl t98_redzone\n"
	".type t98_redzone, @function\n"
	"t98_redzone:\n"
	"	movabsq	$0xa5a5a5a500000000, %rax\n"
	"	movq	$-128, %rcx\n"
	"1:	leaq	(%rax,%rcx), %rdx\n"
	"	movq	%rdx, (%rsp,%rcx)\n"
	"	addq	$8, %rcx\n"
	"	jnz	1b\n"
	"2:	cmpl	$0, (%rdi)\n"
	"	je	2b\n"
	"	xorl	%r8d, %r8d\n"
	"	movq	$-128, %rcx\n"
	"3:	leaq	(%rax,%rcx), %rdx\n"
	"	cmpq	%rdx, (%rsp,%rcx)\n"
	"	je	4f\n"
	"	incl	%r8d\n"
	"4:	addq	$8, %rcx\n"
	"	jnz	3b\n"
	"	movl	%r8d, %eax\n"
	"	ret\n"
	".size t98_redzone, . - t98_redzone\n");

static volatile int alarmed;

static void
alarm_handler(int sig)
{
	volatile char pad[512];	/* use some of the frame's stack */

	pad[0] = 1;
	alarmed = pad[0];
}

/* An asynchronous signal must not clobber the red zone of the leaf
 * function it interrupts (amd64 ABI: 128 bytes below %rsp). */
static void
test_redzone(void)
{
	struct itimerval it;

	subtest = 6;
	install(SIGALRM, alarm_handler, 0);
	alarmed = 0;
	memset(&it, 0, sizeof(it));
	it.it_value.tv_usec = 20000;
	if (setitimer(ITIMER_REAL, &it, NULL) != 0) e(1);
	if (t98_redzone(&alarmed) != 0) e(2);
	signal(SIGALRM, SIG_DFL);
}
#endif

int
main(int argc, char **argv)
{
	start(98);

	test_onstack();
	test_errors();
	test_overflow();
	test_onstack_flag();
#ifdef SA_SIGINFO
	test_uc_stack();
#endif
#if defined(__x86_64__)
	test_redzone();
#endif

	quit();
	return 0;
}
