#include <sys/cdefs.h>
#include <lib.h>
#include "namespace.h"

#include <string.h>
#include <signal.h>

/*
 * sigaltstack(2).  PM keeps the alternate signal stack; whether we are
 * running on it (SS_ONSTACK, and EPERM for changing it then) is decided here,
 * from our own stack pointer.
 */
int
__sigaltstack14(const stack_t *ss, stack_t *oss)
{
	message m;
	stack_t cur;
	char here;
	int onstack;

	memset(&m, 0, sizeof(m));
	if (_syscall(PM_PROC_NR, PM_SIGALTSTACK, &m) < 0)
		return -1;
	cur.ss_sp = m.m_lc_pm_sigaltstack.sp;
	cur.ss_size = m.m_lc_pm_sigaltstack.size;
	cur.ss_flags = m.m_lc_pm_sigaltstack.flags;
	onstack = !(cur.ss_flags & SS_DISABLE) &&
	    &here > (char *)cur.ss_sp &&
	    &here <= (char *)cur.ss_sp + cur.ss_size;

	if (ss != NULL) {
		if (onstack) {
			errno = EPERM;
			return -1;
		}
		memset(&m, 0, sizeof(m));
		m.m_lc_pm_sigaltstack.sp = ss->ss_sp;
		m.m_lc_pm_sigaltstack.size = ss->ss_size;
		m.m_lc_pm_sigaltstack.flags = ss->ss_flags;
		m.m_lc_pm_sigaltstack.set = 1;
		if (_syscall(PM_PROC_NR, PM_SIGALTSTACK, &m) < 0)
			return -1;
	}

	if (oss != NULL) {
		*oss = cur;
		if (onstack)
			oss->ss_flags |= SS_ONSTACK;
	}
	return 0;
}
