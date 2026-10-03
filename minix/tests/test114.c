/* Test 114 - user-supplied program counters and return methods.
 *
 * The kernel returns to user mode with SYSRET or IRETQ, and on x86-64 both
 * fault in kernel mode when the new program counter is not canonical.  Every
 * way for a process to choose its next pc -- sigreturn(2) with a forged
 * context, a signal handler address, an executable's entry point -- must
 * end in an error or a signal for that process, never in a kernel panic.
 * Likewise the kernel entry method recorded in a sigcontext is user data.
 *
 * With an argument, run that one case in this process (for diagnosis).
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <lwp.h>
#include <ucontext.h>

#include <machine/signal.h>

#include "common.h"

int max_error = 3;

#if defined(__x86_64__)
#define KTS_NONE	1
#define KTS_FULLCONTEXT	5
#define KTS_SYSCALL	6
#elif defined(__i386__)
#define KTS_NONE	1
#define KTS_FULLCONTEXT	5
#define KTS_SYSCALL	7
#endif

#define NONCANON	((long)0x0000800000000000ULL)
#define LANDED		42

static long landing_stack[1024] __aligned(16);

static void
landed(void)
{

	_exit(LANDED);
}

/* Return through sigreturn(2) into a forged context. */
static void
forge(int trap_style, long pc)
{
	static struct sigcontext sc;

	memset(&sc, 0, sizeof(sc));
	sigemptyset(&sc.sc_mask);
	sc.trap_style = trap_style;
	sc.sc_magic = SC_MAGIC;
#if defined(__x86_64__)
	sc.sc_rip = pc;
	sc.sc_rsp = (long)&landing_stack[1024 - 1];
	sc.sc_rflags = 0x202;
#elif defined(__i386__)
	sc.sc_eip = pc;
	sc.sc_esp = (long)&landing_stack[1024 - 1];
	sc.sc_eflags = 0x202;
#endif
	(void)sigreturn(&sc);
	/* Refused: the call returns. */
	_exit(errno == EINVAL ? 0 : 1);
}

static void
bad_handler(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = (void (*)(int))NONCANON;
	if (sigaction(SIGUSR1, &sa, NULL) != 0)
		_exit(0);			/* refused up front: fine too */
	raise(SIGUSR1);
	_exit(2);
}

/* The same, for a signal that interrupts the process in user mode, so that
 * the kernel returns into the handler with IRETQ.
 */
static void
bad_handler_async(void)
{
	struct sigaction sa;
	pid_t pid;
	int status;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = (void (*)(int))NONCANON;
	if (sigaction(SIGUSR1, &sa, NULL) != 0)
		_exit(0);
	if ((pid = fork()) < 0) _exit(4);
	if (pid == 0)
		for (;;) ;			/* in user mode when signaled */
	usleep(300000);
	kill(pid, SIGUSR1);
	if (waitpid(pid, &status, 0) != pid) _exit(4);
	/* Killed by the signal it could not catch, or by SIGSEGV. */
	_exit(WIFSIGNALED(status) && (WTERMSIG(status) == SIGUSR1 ||
	    WTERMSIG(status) == SIGSEGV) ? 0 : 5);
}

#if defined(__x86_64__)
/* A thread whose entry point is not canonical.  Nothing checks the entry on
 * the way in, so this one is caught only when the kernel returns to user
 * mode: the thread faults in user mode and SIGSEGV ends the process.
 */
static void
bad_thread(void)
{
	static ucontext_t uc;
	lwpid_t lid;

	memset(&uc, 0, sizeof(uc));
	uc.uc_mcontext.__gregs[_REG_RIP] = NONCANON;
	uc.uc_mcontext.__gregs[_REG_RSP] = (long)&landing_stack[1024 - 1];
	if (_lwp_create(&uc, 0, &lid) != 0)
		_exit(0);			/* refused: fine */
	sleep(5);
	_exit(6);
}
#endif

/* Execute a copy of /bin/sh whose ELF entry point is not canonical. */
static void
bad_entry(void)
{
	char buf[65536];
	int in, out;
	ssize_t n;
	long entry = NONCANON;

	if ((in = open("/bin/sh", O_RDONLY)) < 0) _exit(3);
	if ((out = open("badentry", O_WRONLY | O_CREAT | O_TRUNC, 0755)) < 0)
		_exit(3);
	while ((n = read(in, buf, sizeof(buf))) > 0)
		if (write(out, buf, n) != n) _exit(3);
	/* e_entry: offset 24 in an ELF64 header. */
	if (pwrite(out, &entry, sizeof(entry), 24) != sizeof(entry)) _exit(3);
	close(in);
	close(out);
	execl("./badentry", "badentry", "-c", "exit 7", (char *)NULL);
	_exit(0);				/* refused: fine */
}

static void
run_case(int c)
{

	switch (c) {
	case 'a': forge(KTS_NONE, (long)landed);		/* panicked */
	case 'b': forge(0x7fff, (long)landed);			/* panicked */
	case 'c': forge(0, (long)landed);
	case 'd': forge(KTS_FULLCONTEXT, NONCANON);		/* IRETQ */
	case 'e': forge(KTS_SYSCALL, NONCANON);			/* SYSRET */
	case 'f': forge(KTS_FULLCONTEXT, (long)landed);		/* valid */
	case 'g': bad_handler();
	case 'h': bad_entry();
	case 'i': bad_handler_async();
#if defined(__x86_64__)
	case 'j': bad_thread();
#endif
	}
	_exit(99);
}

/* Run a case in a child; return its wait status. */
static int
in_child(int c)
{
	pid_t pid;
	int status;

	fflush(stdout);
	if ((pid = fork()) < 0) e(90);
	if (pid == 0) {
		alarm(10);
		run_case(c);
	}
	if (waitpid(pid, &status, 0) != pid) e(91);
	return status;
}

static int
exited(int status, int code)
{

	return WIFEXITED(status) && WEXITSTATUS(status) == code;
}

int
main(int argc, char **argv)
{
	int s;

	if (argc > 1) {
		printf("case %c\n", argv[1][0]);
		fflush(stdout);
		run_case(argv[1][0]);
	}

	start(114);

	subtest = 1;
	/* Entry methods that do not exist are refused. */
	if (!exited(in_child('a'), 0)) e(1);
	if (!exited(in_child('b'), 0)) e(2);
	if (!exited(in_child('c'), 0)) e(3);
	/* A valid forged context still works. */
	if (!exited(in_child('f'), LANDED)) e(4);

#if defined(__x86_64__)
	subtest = 2;
	/* A non-canonical pc never reaches SYSRET or IRETQ. */
	s = in_child('d');
	if (!exited(s, 0) && !(WIFSIGNALED(s) && WTERMSIG(s) == SIGSEGV)) e(1);
	s = in_child('e');
	if (!exited(s, 0) && !(WIFSIGNALED(s) && WTERMSIG(s) == SIGSEGV)) e(2);
	s = in_child('g');
	if (!exited(s, 0) && !(WIFSIGNALED(s) && (WTERMSIG(s) == SIGUSR1 ||
	    WTERMSIG(s) == SIGSEGV))) e(3);
	s = in_child('h');
	if (!exited(s, 0) && !(WIFSIGNALED(s) && WTERMSIG(s) == SIGSEGV)) e(4);
	if (!exited(in_child('i'), 0)) e(5);
	s = in_child('j');
	if (!exited(s, 0) && !(WIFSIGNALED(s) && WTERMSIG(s) == SIGSEGV)) e(6);
	unlink("badentry");
#endif
	(void)s;

	quit();
	return 0;
}
