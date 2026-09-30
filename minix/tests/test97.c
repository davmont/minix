/* Test 97 - SA_SIGINFO signal handlers.
 *
 * A handler installed with SA_SIGINFO gets (signo, siginfo_t *, ucontext_t *):
 * the siginfo_t says where the signal came from -- the sender's pid and uid
 * for kill(2), the child and its fate for SIGCHLD, the address and cause of a
 * SIGSEGV -- and the ucontext_t holds
 * the interrupted context.  A plain handler keeps working as before.
 */
#include <sys/mman.h>
#include <sys/wait.h>
#include <setjmp.h>
#include <signal.h>
#include <ucontext.h>

#include "common.h"

#ifndef SA_SIGINFO
/* SA_SIGINFO is implemented on amd64 only; nothing to test here. */
int
main(int argc, char **argv)
{
	start(97);
	quit();
	return 0;
}
#else

int max_error = 0;

static volatile int got;
static siginfo_t info;
static int info_ok, uc_ok;
static long uc_rsp;

static void
handler(int sig, siginfo_t *si, void *ctx)
{
	ucontext_t *uc = ctx;

	got = sig;
	info_ok = (si != NULL);
	if (si != NULL)
		info = *si;
	uc_ok = (uc != NULL && (uc->uc_flags & _UC_CPU));
	if (uc_ok)
		uc_rsp = (long)_UC_MACHINE_SP(uc);
}

static void
plain_handler(int sig)
{
	got = sig;
}

static void
install(int sig, int flags)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = flags;
	if (flags & SA_SIGINFO)
		sa.sa_sigaction = handler;
	else
		sa.sa_handler = plain_handler;
	if (sigaction(sig, &sa, NULL) != 0) e(90);
}

/* kill(2) to ourselves: SI_USER, our pid and uid, a usable context. */
static void
test_kill(void)
{
	long here;

	subtest = 1;
	install(SIGUSR1, SA_SIGINFO);
	got = 0;
	if (kill(getpid(), SIGUSR1) != 0) e(1);
	if (got != SIGUSR1) e(2);
	if (!info_ok) e(3);
	if (info.si_signo != SIGUSR1) e(4);
	if (info.si_code != SI_USER) e(5);
	if (info.si_pid != getpid()) e(6);
	if (info.si_uid != getuid()) e(7);
	if (!uc_ok) e(8);
	/* The interrupted stack pointer is somewhere near our own frame. */
	here = (long)&here;
	if (uc_rsp < here - 65536 || uc_rsp > here + 65536) e(9);
}

/* SIGCHLD: the child's pid, CLD_EXITED and its exit status; or CLD_KILLED
 * and the signal. */
static void
test_sigchld(void)
{
	sigset_t set, old;
	pid_t pid;
	int status;

	subtest = 2;
	install(SIGCHLD, SA_SIGINFO);

	sigemptyset(&set);
	sigaddset(&set, SIGCHLD);
	sigprocmask(SIG_BLOCK, &set, &old);
	got = 0;
	if ((pid = fork()) == 0)
		_exit(42);
	if (pid < 0) e(1);
	sigsuspend(&old);		/* wait for SIGCHLD */
	sigprocmask(SIG_SETMASK, &old, NULL);
	if (got != SIGCHLD) e(2);
	if (info.si_signo != SIGCHLD) e(3);
	if (info.si_code != CLD_EXITED) e(4);
	if (info.si_pid != pid) e(5);
	if (info.si_status != 42) e(6);
	if (waitpid(pid, &status, 0) != pid) e(7);

	sigprocmask(SIG_BLOCK, &set, &old);
	got = 0;
	if ((pid = fork()) == 0) {
		signal(SIGTERM, SIG_DFL);
		pause();
		_exit(0);
	}
	if (pid < 0) e(8);
	if (kill(pid, SIGTERM) != 0) e(9);
	sigsuspend(&old);
	sigprocmask(SIG_SETMASK, &old, NULL);
	if (got != SIGCHLD) e(10);
	if (info.si_code != CLD_KILLED) e(11);
	if (info.si_pid != pid) e(12);
	if (info.si_status != SIGTERM) e(13);
	if (waitpid(pid, &status, 0) != pid) e(14);
	signal(SIGCHLD, SIG_DFL);
}

static sigjmp_buf jb;

static void
segv_handler(int sig, siginfo_t *si, void *ctx)
{
	handler(sig, si, ctx);
	siglongjmp(jb, 1);
}

/* A bad access: SIGSEGV with the address and why -- SEGV_ACCERR for a page
 * the process may not write, SEGV_MAPERR for one that is not mapped. */
static void
test_segv(void)
{
	struct sigaction sa;
	volatile char *p;
	long pagesize = sysconf(_SC_PAGESIZE);

	subtest = 4;
	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = segv_handler;
	if (sigaction(SIGSEGV, &sa, NULL) != 0) e(1);

	p = mmap(NULL, 2 * pagesize, PROT_READ, MAP_ANON | MAP_PRIVATE, -1, 0);
	if (p == MAP_FAILED) e(2);

	got = 0;
	if (sigsetjmp(jb, 1) == 0)
		p[100] = 1;
	if (got != SIGSEGV) e(3);
	if (info.si_signo != SIGSEGV) e(4);
	if (info.si_code != SEGV_ACCERR) e(5);
	if (info.si_addr != (void *)(p + 100)) e(6);

	if (munmap((void *)(p + pagesize), pagesize) != 0) e(7);
	got = 0;
	if (sigsetjmp(jb, 1) == 0)
		(void)p[pagesize + 8];
	if (got != SIGSEGV) e(8);
	if (info.si_code != SEGV_MAPERR) e(9);
	if (info.si_addr != (void *)(p + pagesize + 8)) e(10);

	munmap((void *)p, pagesize);
	signal(SIGSEGV, SIG_DFL);
}

/* A handler without SA_SIGINFO still just gets the signal number. */
static void
test_plain(void)
{
	subtest = 3;
	install(SIGUSR2, 0);
	got = 0;
	if (kill(getpid(), SIGUSR2) != 0) e(1);
	if (got != SIGUSR2) e(2);
}

int
main(int argc, char **argv)
{
	start(97);

	test_kill();
	test_sigchld();
	test_segv();
	test_plain();

	quit();
	return 0;
}
#endif /* SA_SIGINFO */
