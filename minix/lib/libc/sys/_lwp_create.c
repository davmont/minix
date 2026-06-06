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

	/*
	 * Prefer the stack pointer prepared in the context (this is what
	 * _lwp_makecontext() sets, already laid out per the SysV AMD64 ABI).
	 * If the caller only supplied uc_stack (no _REG_RSP), derive the top of
	 * the stack ourselves: 16-byte aligned and then biased by 8 so that the
	 * thread enters its start function with rsp == 8 (mod 16), exactly as if
	 * a CALL had pushed a return address.  Without this bias the first SSE
	 * access in the callee (e.g. the movaps that zeroes a message buffer)
	 * faults with #GP on the misaligned stack slot.
	 */
	sp = (uintptr_t)ucp->uc_mcontext.__gregs[_REG_RSP];
	if (sp == 0) {
		sp = (uintptr_t)ucp->uc_stack.ss_sp + ucp->uc_stack.ss_size;
		sp &= ~(uintptr_t)15;
		sp -= 8;
	}

	memset(&m, 0, sizeof(m));
	m.m_lc_pm_lwp_create.entry = ucp->uc_mcontext.__gregs[_REG_RIP];
	m.m_lc_pm_lwp_create.stack = sp;
	m.m_lc_pm_lwp_create.arg = ucp->uc_mcontext.__gregs[_REG_RDI];
	m.m_lc_pm_lwp_create.tlsbase = ucp->uc_mcontext._mc_tlsbase;
	m.m_lc_pm_lwp_create.flags = flags;

	if (_syscall(PM_PROC_NR, PM_LWP_CREATE, &m) < 0)
		return -1;

	/* PM returns the new thread's lwpid (its kernel endpoint). */
	if (new_lwp != NULL)
		*new_lwp = (lwpid_t) m.m_pm_lc_lwp.lwpid;
	return 0;
}
