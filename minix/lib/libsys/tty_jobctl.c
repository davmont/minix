#include "syslib.h"

#include <string.h>

/*
 * Ask PM a job control question on behalf of a terminal driver (PM_TTYJC):
 * see the TTYJC_* operations in <minix/ipc.h>.  For TTYJC_GETIDS, PM also
 * returns the process group and session of 'endpt'.
 */
int
tty_jobctl(int op, endpoint_t endpt, pid_t pgrp, pid_t session, int sig,
	pid_t *ret_pgrp, pid_t *ret_session)
{
	message m;
	int r;

	memset(&m, 0, sizeof(m));
	m.m_lsys_pm_ttyjc.op = op;
	m.m_lsys_pm_ttyjc.endpt = endpt;
	m.m_lsys_pm_ttyjc.pgrp = pgrp;
	m.m_lsys_pm_ttyjc.session = session;
	m.m_lsys_pm_ttyjc.sig = sig;
	r = _taskcall(PM_PROC_NR, PM_TTYJC, &m);
	if (ret_pgrp != NULL) *ret_pgrp = m.m_pm_lsys_ttyjc.pgrp;
	if (ret_session != NULL) *ret_session = m.m_pm_lsys_ttyjc.session;
	return r;
}
