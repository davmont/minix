#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <string.h>
#include <unistd.h>

/*
 * setresuid(2), setresgid(2): set the real, effective and saved user or group
 * id; -1 leaves one unchanged.  The super-user may set any id, others only one
 * they already have.
 */
static int
setres(int call, uint32_t r, uint32_t e, uint32_t s)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lc_pm_setresid.rid = r;
	m.m_lc_pm_setresid.eid = e;
	m.m_lc_pm_setresid.sid = s;

	return _syscall(PM_PROC_NR, call, &m);
}

int
setresuid(uid_t ruid, uid_t euid, uid_t suid)
{

	return setres(PM_SETRESUID, ruid, euid, suid);
}

int
setresgid(gid_t rgid, gid_t egid, gid_t sgid)
{

	return setres(PM_SETRESGID, rgid, egid, sgid);
}

/*
 * getresuid(2), getresgid(2): get the real, effective and saved ids.
 */
static int
getres(message *m)
{

	memset(m, 0, sizeof(*m));
	return _syscall(PM_PROC_NR, PM_GETRESID, m);
}

int
getresuid(uid_t *ruid, uid_t *euid, uid_t *suid)
{
	message m;

	if (getres(&m) < 0)
		return -1;
	if (ruid != NULL) *ruid = m.m_pm_lc_getresid.ruid;
	if (euid != NULL) *euid = m.m_pm_lc_getresid.euid;
	if (suid != NULL) *suid = m.m_pm_lc_getresid.suid;
	return 0;
}

int
getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid)
{
	message m;

	if (getres(&m) < 0)
		return -1;
	if (rgid != NULL) *rgid = m.m_pm_lc_getresid.rgid;
	if (egid != NULL) *egid = m.m_pm_lc_getresid.egid;
	if (sgid != NULL) *sgid = m.m_pm_lc_getresid.sgid;
	return 0;
}
