#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <lwp.h>
#include <string.h>

/*
 * Terminate the calling thread (LWP).  PM tears the thread down without
 * disturbing the rest of the process; the call does not return.
 */
int
_lwp_exit(void)
{
	message m;

	memset(&m, 0, sizeof(m));
	(void)_syscall(PM_PROC_NR, PM_LWP_EXIT, &m);

	/* PM destroys this thread, so the syscall never returns.  Spin as a
	 * safety net in case it ever does. */
	for (;;)
		;
	/* NOTREACHED */
	return -1;
}
