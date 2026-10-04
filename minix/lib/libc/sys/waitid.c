#include <sys/cdefs.h>
#include <lib.h>
#include "namespace.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>

/*
 * waitid(2): wait for a child chosen by 'idtype' and 'id' to exit, stop or
 * continue, as 'options' asks, and describe what happened in '*info'.  PM
 * fills in the siginfo_t; with WNOHANG and nothing to report it stays zeroed
 * (si_pid 0), as POSIX says.
 */
int
waitid(idtype_t idtype, id_t id, siginfo_t *info, int options)
{
	message m;

	if (info == NULL) {
		errno = EFAULT;
		return -1;
	}
	memset(info, 0, sizeof(*info));

	memset(&m, 0, sizeof(m));
	m.m_lc_pm_wait4.pid = (pid_t)id;
	m.m_lc_pm_wait4.options = options;
	m.m_lc_pm_wait4.idtype = idtype;
	m.m_lc_pm_wait4.waitid = 1;
	m.m_lc_pm_wait4.info = (vir_bytes)info;

	if (_syscall(PM_PROC_NR, PM_WAIT4, &m) < 0)
		return -1;
	return 0;
}
