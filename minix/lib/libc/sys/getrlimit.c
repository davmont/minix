/*	getrlimit                             Author: Erik van der Kouwe
 *      query resource consumtion limits      4 December 2009
 *
 * Based on these specifications:
 * http://www.opengroup.org/onlinepubs/007908775/xsh/getdtablesize.html 
 * http://www.opengroup.org/onlinepubs/007908775/xsh/getrlimit.html 
 */
 
#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

int getrlimit(int resource, struct rlimit *rlp)
{
	rlim_t limit;

	switch (resource)
	{
		case RLIMIT_STACK:
			/* Report a finite stack limit.  libpthread sizes every
			 * thread stack from RLIMIT_STACK's soft limit; returning
			 * RLIM_INFINITY (as the other resources do) makes it try
			 * to mmap an astronomically large per-thread stack, which
			 * fails with ENOMEM and breaks pthread_create().  Use the
			 * conventional 8 MB soft / 64 MB hard limits.
			 */
			rlp->rlim_cur = (rlim_t) 8 * 1024 * 1024;
			rlp->rlim_max = (rlim_t) 64 * 1024 * 1024;
			return 0;

		case RLIMIT_AS:
		/* case RLIMIT_VMEM: Same as RLIMIT_AS */
		case RLIMIT_DATA:
			/* Enforced by VM; report the stored soft limit so
			 * get/set are consistent (VM uses 0 = unlimited). */
			{
				message m;
				memset(&m, 0, sizeof(m));
				m.m_lc_vm_rlimit.which = resource;
				m.m_lc_vm_rlimit.op = VMRL_GET;
				if (_syscall(VM_PROC_NR, VM_RLIMIT, &m) < 0)
					return -1;
				limit = (m.m_lc_vm_rlimit.limit == 0) ?
				    RLIM_INFINITY : (rlim_t) m.m_lc_vm_rlimit.limit;
			}
			break;

		case RLIMIT_CPU:
		case RLIMIT_FSIZE:
		case RLIMIT_CORE:
		case RLIMIT_RSS:
		case RLIMIT_MEMLOCK:
		case RLIMIT_SBSIZE:
		case RLIMIT_NTHR:
			/* no limit enforced (however architectural limits
			 * may apply)
			 */
			limit = RLIM_INFINITY;
			break;

		case RLIMIT_NPROC:
			limit = CHILD_MAX;
			break;

		case RLIMIT_NOFILE:
			limit = OPEN_MAX;
			break;

		default:
			errno = EINVAL;
			return -1;
	}		

	/* return limit */
	rlp->rlim_cur = limit;
	rlp->rlim_max = limit;
	return 0;
}

