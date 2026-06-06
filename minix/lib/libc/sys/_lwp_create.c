#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <lwp.h>
#include <ucontext.h>
#include <string.h>

/*
 * Create a new LWP (thread) sharing this process's address space.  The
 * ucontext supplies the new thread's entry point, stack, first argument and
 * TLS base (see _lwp_makecontext()).  Implemented on MINIX as a PM call that
 * forks a process which shares the parent's page-table root (CR3).
 */
int
_lwp_create(const ucontext_t *ucp, unsigned long flags, lwpid_t *new_lwp)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lc_pm_lwp_create.ctx = (vir_bytes) ucp;
	m.m_lc_pm_lwp_create.flags = flags;
	m.m_lc_pm_lwp_create.newlid = (vir_bytes) new_lwp;

	return _syscall(PM_PROC_NR, PM_LWP_CREATE, &m);
}
