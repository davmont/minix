#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <sys/types.h>
#include <lwp.h>
#include <string.h>
#include <stdint.h>

/*
 * Wake the LWP identified by 'lwp' (its lwpid).  If it is not parked yet, PM
 * records the unpark so the LWP's next _lwp_park() returns immediately.
 */
int
_lwp_unpark(lwpid_t lwp, const void *hint)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lc_pm_lwp_unpark.target = (int32_t) lwp;
	(void) hint;

	return _syscall(PM_PROC_NR, PM_LWP_UNPARK, &m);
}

/*
 * Wake several LWPs.  With a NULL target list, report the maximum batch size
 * we support (NetBSD semantics).  PM has no batch call, so we simply issue one
 * unpark per target.
 */
ssize_t
_lwp_unpark_all(const lwpid_t *targets, size_t ntargets, const void *hint)
{
	size_t i;

	if (targets == NULL)
		return (ssize_t) 1024;

	for (i = 0; i < ntargets; i++)
		(void) _lwp_unpark(targets[i], hint);

	return (ssize_t) ntargets;
}
