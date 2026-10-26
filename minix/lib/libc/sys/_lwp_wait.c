#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <lwp.h>
#include <string.h>
#include <stdint.h>

/*
 * Wait for (join) the LWP 'wait_for' to terminate, reaping it.  wait_for == 0
 * joins any joinable sibling.  On success returns 0 and, if 'departed' is not
 * NULL, stores the lwpid that terminated.  PM blocks the caller until the
 * target exits.
 */
int
_lwp_wait(lwpid_t wait_for, lwpid_t *departed)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lc_pm_lwp_wait.wait_for = (int32_t) wait_for;

	if (_syscall(PM_PROC_NR, PM_LWP_WAIT, &m) < 0)
		return -1;

	if (departed != NULL)
		*departed = (lwpid_t) m.m_pm_lc_lwp.lwpid;
	return 0;
}

/*
 * Detach the LWP 'lwp': it can no longer be joined, and PM frees it as it
 * exits, or now if it has exited already.  The message is _lwp_wait()'s.
 */
int
_lwp_detach(lwpid_t lwp)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lc_pm_lwp_wait.wait_for = (int32_t) lwp;
	return _syscall(PM_PROC_NR, PM_LWP_DETACH, &m);
}
