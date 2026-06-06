#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <lwp.h>
#include <string.h>
#include <stdint.h>

/*
 * Return the calling thread's lwpid.  PM reports the thread's kernel endpoint
 * as its lwpid (unique and stable for the thread's lifetime).
 */
lwpid_t
_lwp_self(void)
{
	message m;

	memset(&m, 0, sizeof(m));
	if (_syscall(PM_PROC_NR, PM_LWP_SELF, &m) < 0)
		return 0;
	return (lwpid_t) m.m_pm_lc_lwp.lwpid;
}
