/* Test 137 - the kernel does not take user flags into the kernel.
 *
 * SYSCALL clears in RFLAGS only what the FMASK MSR says.  When that was IF
 * alone, a process entered the kernel with the flags it chose: DF set made
 * the kernel's memcpy() and memset() run backwards, AC set lifted SMAP, and
 * TF set raised a debug trap on the first instruction of the SYSCALL entry,
 * before it had switched to the kernel stack.
 *
 * Each subtest makes calls to PM directly with SYSCALL, with one of these
 * flags set by the instruction before it, and checks the replies.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <minix/ipc.h>
#include <minix/callnr.h>
#include <minix/com.h>
#include <minix/ipcconst.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

#define FLAG_TF		0x00000100UL
#define FLAG_DF		0x00000400UL
#define FLAG_NT		0x00004000UL
#define FLAG_AC		0x00040000UL

#define CALLS		10000

/* SENDREC 'm' to PM with SYSCALL, after setting 'flags' in RFLAGS.  The
 * flags are set by the POPF right before the SYSCALL, so that TF traps on
 * the instruction after the SYSCALL: the kernel's, unless it clears TF.
 * The red zone is stepped over, as the PUSHF would overwrite it.
 */
static long
raw_sendrec(message *m, unsigned long flags)
{
	long ax = PM_PROC_NR, bx = SENDREC, di = IPCVEC_UM;
	register long r10 __asm__("r10") = (long)m;

	__asm__ __volatile__(
	    "sub	$128, %%rsp\n\t"
	    "pushfq\n\t"
	    "or	%[flags], (%%rsp)\n\t"
	    "popfq\n\t"
	    "syscall\n\t"
	    "cld\n\t"
	    "lea	128(%%rsp), %%rsp\n\t"
	    : "+a" (ax), "+b" (bx), "+D" (di), "+r" (r10)
	    : [flags] "r" (flags)
	    : "rcx", "rdx", "rsi", "r8", "r9", "r11", "memory", "cc");
	return ax;
}

/* Clear the flags that the kernel hands back, if it does. */
static void
clear_flags(void)
{

	__asm__ __volatile__(
	    "sub	$128, %%rsp\n\t"
	    "pushfq\n\t"
	    "andq	%0, (%%rsp)\n\t"
	    "popfq\n\t"
	    "lea	128(%%rsp), %%rsp\n\t"
	    : : "r" (~(FLAG_DF | FLAG_NT | FLAG_AC)) : "memory", "cc");
}

static void
test_flag(int sub, unsigned long flag)
{
	message m;
	pid_t me = getpid(), parent = getppid();
	int i;

	subtest = sub;
	for (i = 0; i < CALLS; i++) {
		memset(&m, 0, sizeof(m));
		m.m_type = PM_GETPID;
		if (raw_sendrec(&m, flag) != 0) {
			clear_flags();
			e(1);
			break;
		}
		clear_flags();
		if (m.m_type != me || m.m_pm_lc_getpid.parent_pid != parent) {
			e(2);
			break;
		}
	}
	/* The process and the system still work. */
	if (getpid() != me || getppid() != parent) e(3);
}

/* With TF, the trap belongs to the process, after the SYSCALL returns:
 * the child dies of SIGTRAP or, if the kernel ignores TF, exits.
 */
static void
test_trace(void)
{
	message m;
	pid_t pid;
	int status;

	subtest = 4;
	if ((pid = fork()) == 0) {
		signal(SIGTRAP, SIG_DFL);
		memset(&m, 0, sizeof(m));
		m.m_type = PM_GETPID;
		(void)raw_sendrec(&m, FLAG_TF);
		_exit(0);
	}
	if (pid < 0) e(1);
	if (waitpid(pid, &status, 0) != pid) e(2);
	if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGTRAP) &&
	    !(WIFEXITED(status) && WEXITSTATUS(status) == 0))
		e(3);
	if (getpid() <= 0) e(4);
}

int
main(int argc, char **argv)
{

	start(137);

	test_flag(1, FLAG_DF);
	test_flag(2, FLAG_AC);
	test_flag(3, FLAG_NT);
	test_trace();

	quit();
	return 0;
}
