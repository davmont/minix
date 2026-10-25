#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <sys/types.h>
#include <lwp.h>
#include <string.h>
#include <stdint.h>

/*
 * Interrupt the sleep of the LWP 'lwp' of this process: a call it is blocked
 * in returns EINTR, or, if it is not blocked, its next one does.
 * pthread_cancel() uses this to reach a thread at a cancellation point.
 */
int
_lwp_wakeup(lwpid_t lwp)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lc_pm_lwp_unpark.target = (int32_t) lwp;

	return _syscall(PM_PROC_NR, PM_LWP_WAKEUP, &m);
}
