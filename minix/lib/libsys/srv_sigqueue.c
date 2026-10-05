#include "syslib.h"

#include <string.h>

/*
 * Send signal 'sig' to process 'pid' with siginfo code 'code' and value
 * 'value' (PM_SRV_SIGQUEUE), as a system service may, for example for a
 * message queue notification (SI_MESGQ).
 */
int
srv_sigqueue(pid_t pid, int sig, int code, vir_bytes value)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lsys_pm_sigqueue.pid = pid;
	m.m_lsys_pm_sigqueue.sig = sig;
	m.m_lsys_pm_sigqueue.code = code;
	m.m_lsys_pm_sigqueue.value = value;
	return _taskcall(PM_PROC_NR, PM_SRV_SIGQUEUE, &m);
}
