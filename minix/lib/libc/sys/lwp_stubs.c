#include <sys/cdefs.h>
#include <lib.h>
#include "namespace.h"

#include <string.h>
#include <lwp.h>
#include <sched.h>
#include <errno.h>

/*
 * Stubs for thread/scheduling primitives that libpthread references but MINIX
 * does not yet implement.  None are on the core mutex/cond/join path.
 */

/*
 * Per-thread (LWP-directed) signal, the core of pthread_kill(3): PM delivers
 * it to that thread only, if it belongs to our process.
 */
int
_lwp_kill(lwpid_t lwp, int sig)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lc_pm_lwp_kill.target = lwp;
	m.m_lc_pm_lwp_kill.sig = sig;
	return _syscall(PM_PROC_NR, PM_LWP_KILL, &m);
}

/*
 * Priority-ceiling protection for PTHREAD_PRIO_PROTECT mutexes.  MINIX has no
 * per-thread priority-ceiling call; treat it as a no-op.  Such mutexes still
 * provide mutual exclusion, just without priority elevation.
 */
int
_sched_protect(int priority)
{
	(void)priority;
	return 0;
}
