#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

/*
 * RLIMIT_AS and RLIMIT_DATA are enforced by VM at mmap()/brk(): the soft limit
 * is handed to VM, which fails an allocation that would push the process past
 * it (ENOMEM), so a runaway process fails its own allocation rather than
 * driving the whole system toward OOM.  The remaining resources are accepted
 * but not enforced (as before).
 */
int setrlimit(int resource, const struct rlimit *rlp)
{
	message m;

	switch (resource)
	{
		case RLIMIT_AS:
		case RLIMIT_DATA:
			if (rlp == NULL) { errno = EFAULT; return -1; }
			memset(&m, 0, sizeof(m));
			m.m_lc_vm_rlimit.which = resource;
			m.m_lc_vm_rlimit.op = VMRL_SET;
			/* VM uses 0 to mean "unlimited". */
			m.m_lc_vm_rlimit.limit =
			    (rlp->rlim_cur == RLIM_INFINITY) ? 0 :
			    (uint64_t) rlp->rlim_cur;
			if (_syscall(VM_PROC_NR, VM_RLIMIT, &m) < 0)
				return -1;
			return 0;

		case RLIMIT_CPU:
		case RLIMIT_FSIZE:
		case RLIMIT_STACK:
		case RLIMIT_CORE:
		case RLIMIT_RSS:
		case RLIMIT_MEMLOCK:
		case RLIMIT_NPROC:
		case RLIMIT_NOFILE:
		case RLIMIT_SBSIZE:
		case RLIMIT_NTHR:
			break;

		default:
			errno = EINVAL;
			return -1;
	}

	return 0;
}
