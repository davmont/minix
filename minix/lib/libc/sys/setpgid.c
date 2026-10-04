#include <sys/cdefs.h>
#include <lib.h>
#include "namespace.h"

#include <string.h>
#include <unistd.h>

/*
 * setpgid(2): move process 'pid' (0: the caller) to process group 'pgid'
 * (0: its own pid), within the caller's session.  PM checks the rules.
 */
int setpgid(pid_t pid, pid_t pgid)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lc_pm_setpgid.pid = pid;
	m.m_lc_pm_setpgid.pgid = pgid;
	return _syscall(PM_PROC_NR, PM_SETPGID, &m) < 0 ? -1 : 0;
}

/* getpgid(2): the process group of process 'pid' (0: the caller). */
pid_t getpgid(pid_t pid)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lc_pm_getsid.pid = pid;
	return _syscall(PM_PROC_NR, PM_GETPGID, &m);
}
