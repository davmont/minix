#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <lwp.h>
#include <ucontext.h>
#include <string.h>
#include <stdint.h>

/*
 * Create a new LWP (thread) sharing this process's address space.  The
 * ucontext supplies the new thread's entry point, stack, first argument and
 * TLS base (see _lwp_makecontext()).  We extract those fields here and pass
 * them to PM, which forks a process sharing the parent's page-table root
 * (CR3) and points it at the entry/stack.
 */
int
_lwp_create(const ucontext_t *ucp, unsigned long flags, lwpid_t *new_lwp)
{
	message m;
	uintptr_t sp;

	if (ucp == NULL)
		return EINVAL;

	/* Top of the thread stack, 16-byte aligned. */
	sp = (uintptr_t)ucp->uc_stack.ss_sp + ucp->uc_stack.ss_size;
	sp &= ~(uintptr_t)15;

	memset(&m, 0, sizeof(m));
	m.m_lc_pm_lwp_create.entry = ucp->uc_mcontext.__gregs[_REG_RIP];
	m.m_lc_pm_lwp_create.stack = sp;
	m.m_lc_pm_lwp_create.arg = ucp->uc_mcontext.__gregs[_REG_RDI];
	m.m_lc_pm_lwp_create.tlsbase = ucp->uc_mcontext._mc_tlsbase;
	m.m_lc_pm_lwp_create.flags = flags;

	(void)new_lwp;	/* lwpid not yet returned to the caller */
	return _syscall(PM_PROC_NR, PM_LWP_CREATE, &m);
}
