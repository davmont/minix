/* This file handles the 6 system calls that get and set uids and gids.
 * It also handles getpid(), setsid(), and getpgrp().  The code for each
 * one is so tiny that it hardly seemed worthwhile to make each a separate
 * function.
 */

#include "pm.h"
#include <minix/callnr.h>
#include <minix/endpoint.h>
#include <limits.h>
#include <minix/com.h>
#include <signal.h>
#include "mproc.h"

/*===========================================================================*
 *				do_get					     *
 *===========================================================================*/
int
do_get(void)
{
/* Handle PM_GETUID, PM_GETGID, PM_GETGROUPS, PM_GETPID, PM_GETPGRP, PM_GETSID,
 * PM_ISSETUGID.
 */
  register struct mproc *rmp = mp;
  int r;
  int ngroups;

  switch(call_nr) {
	case PM_GETGROUPS:
		ngroups = m_in.m_lc_pm_groups.num;
		if (ngroups > NGROUPS_MAX || ngroups < 0)
			return(EINVAL);

		if (ngroups == 0) {
			r = rmp->mp_ngroups;
			break;
		}

		if (ngroups < rmp->mp_ngroups)
			/* Asking for less groups than available */
			return(EINVAL);

		r = sys_datacopy(SELF, (vir_bytes) rmp->mp_sgroups, who_e,
			m_in.m_lc_pm_groups.ptr, ngroups * sizeof(gid_t));

		if (r != OK)
			return(r);

		r = rmp->mp_ngroups;
		break;
	case PM_GETUID:
		r = rmp->mp_realuid;
		rmp->mp_reply.m_pm_lc_getuid.euid = rmp->mp_effuid;
		break;

	case PM_GETGID:
		r = rmp->mp_realgid;
		rmp->mp_reply.m_pm_lc_getgid.egid = rmp->mp_effgid;
		break;

	case PM_GETRESID:
		rmp->mp_reply.m_pm_lc_getresid.ruid = rmp->mp_realuid;
		rmp->mp_reply.m_pm_lc_getresid.euid = rmp->mp_effuid;
		rmp->mp_reply.m_pm_lc_getresid.suid = rmp->mp_svuid;
		rmp->mp_reply.m_pm_lc_getresid.rgid = rmp->mp_realgid;
		rmp->mp_reply.m_pm_lc_getresid.egid = rmp->mp_effgid;
		rmp->mp_reply.m_pm_lc_getresid.sgid = rmp->mp_svgid;
		r = OK;
		break;

	case PM_GETPID:
	{
		/* Threads created via _lwp_create() share the process identity of
		 * their group leader, so getpid()/getppid() must resolve through it. */
		struct mproc *idp = rmp;
		if (rmp->mp_lwp_group != NO_LWP_GROUP)
			idp = &mproc[rmp->mp_lwp_group];
		r = idp->mp_pid;
		rmp->mp_reply.m_pm_lc_getpid.parent_pid = mproc[idp->mp_parent].mp_pid;
		break;
	}

	case PM_GETPGRP:
		r = rmp->mp_procgrp;
		break;

	case PM_GETSID:
	{
		struct mproc *target;
		pid_t p = m_in.m_lc_pm_getsid.pid;
		target = p ? find_proc(p) : &mproc[who_p];
		r = ESRCH;
		if(target)
			r = target->mp_procgrp;
		break;
	}
	case PM_ISSETUGID:
		r = !!(rmp->mp_flags & TAINTED);
		break;

	default:
		r = EINVAL;
		break;
  }
  return(r);
}

/*===========================================================================*
 *				set_resid				     *
 *===========================================================================*/
static int
set_resid(struct mproc *rmp, uint32_t *realp, uint32_t *effp, uint32_t *savp)
{
/* Perform setresuid() or setresgid(): set the real, effective and saved ids
 * at 'realp', 'effp' and 'savp' to those in the request; (uid_t)-1 keeps one.
 * The super-user may set any id; others only an id they already have, real,
 * effective or saved.  All three change, or none.
 */
  uint32_t ids[3], cur[3];
  int i, j;

  ids[0] = m_in.m_lc_pm_setresid.rid;
  ids[1] = m_in.m_lc_pm_setresid.eid;
  ids[2] = m_in.m_lc_pm_setresid.sid;
  cur[0] = *realp;
  cur[1] = *effp;
  cur[2] = *savp;

  if (rmp->mp_effuid != SUPER_USER) {
	for (i = 0; i < 3; i++) {
		if (ids[i] == (uint32_t)-1)
			continue;
		for (j = 0; j < 3; j++)
			if (ids[i] == cur[j])
				break;
		if (j == 3)
			return(EPERM);
	}
  }

  if (ids[0] != (uint32_t)-1) *realp = ids[0];
  if (ids[1] != (uint32_t)-1) *effp = ids[1];
  if (ids[2] != (uint32_t)-1) *savp = ids[2];
  return(OK);
}

/*===========================================================================*
 *				do_set					     *
 *===========================================================================*/
int
do_set(void)
{
/* Handle PM_SETUID, PM_SETEUID, PM_SETGID, PM_SETGROUPS, PM_SETEGID,
 * PM_SETRESUID, PM_SETRESGID and SETSID. These calls have in common that, if
 * successful, they will be forwarded to VFS as well.
 */
  register struct mproc *rmp = mp;
  message m;
  int r, i;
  int ngroups;
  uid_t uid;
  gid_t gid;

  memset(&m, 0, sizeof(m));

  switch(call_nr) {
	case PM_SETUID:
		uid = m_in.m_lc_pm_setuid.uid;
		/* NetBSD specific semantics: setuid(geteuid()) may fail. */
		if (rmp->mp_realuid != uid && rmp->mp_effuid != SUPER_USER)
			return(EPERM);
		/* BSD semantics: always update all three fields. */
		rmp->mp_realuid = uid;
		rmp->mp_effuid = uid;
		rmp->mp_svuid = uid;

		m.m_type = VFS_PM_SETUID;
		m.VFS_PM_ENDPT = rmp->mp_endpoint;
		m.VFS_PM_EID = rmp->mp_effuid;
		m.VFS_PM_RID = rmp->mp_realuid;

		break;

	case PM_SETEUID:
		uid = m_in.m_lc_pm_setuid.uid;
		/* BSD semantics: seteuid(geteuid()) may fail. */
		if (rmp->mp_realuid != uid && rmp->mp_svuid != uid &&
		    rmp->mp_effuid != SUPER_USER)
			return(EPERM);
		rmp->mp_effuid = uid;

		m.m_type = VFS_PM_SETUID;
		m.VFS_PM_ENDPT = rmp->mp_endpoint;
		m.VFS_PM_EID = rmp->mp_effuid;
		m.VFS_PM_RID = rmp->mp_realuid;

		break;

	case PM_SETGID:
		gid = m_in.m_lc_pm_setgid.gid;
		if (rmp->mp_realgid != gid && rmp->mp_effuid != SUPER_USER)
			return(EPERM);
		rmp->mp_realgid = gid;
		rmp->mp_effgid = gid;
		rmp->mp_svgid = gid;

		m.m_type = VFS_PM_SETGID;
		m.VFS_PM_ENDPT = rmp->mp_endpoint;
		m.VFS_PM_EID = rmp->mp_effgid;
		m.VFS_PM_RID = rmp->mp_realgid;

		break;

	case PM_SETEGID:
		gid = m_in.m_lc_pm_setgid.gid;
		if (rmp->mp_realgid != gid && rmp->mp_svgid != gid &&
		    rmp->mp_effuid != SUPER_USER)
			return(EPERM);
		rmp->mp_effgid = gid;

		m.m_type = VFS_PM_SETGID;
		m.VFS_PM_ENDPT = rmp->mp_endpoint;
		m.VFS_PM_EID = rmp->mp_effgid;
		m.VFS_PM_RID = rmp->mp_realgid;

		break;

	case PM_SETRESUID:
		r = set_resid(rmp, &rmp->mp_realuid, &rmp->mp_effuid,
		    &rmp->mp_svuid);
		if (r != OK)
			return(r);

		m.m_type = VFS_PM_SETUID;
		m.VFS_PM_ENDPT = rmp->mp_endpoint;
		m.VFS_PM_EID = rmp->mp_effuid;
		m.VFS_PM_RID = rmp->mp_realuid;

		break;

	case PM_SETRESGID:
		r = set_resid(rmp, &rmp->mp_realgid, &rmp->mp_effgid,
		    &rmp->mp_svgid);
		if (r != OK)
			return(r);

		m.m_type = VFS_PM_SETGID;
		m.VFS_PM_ENDPT = rmp->mp_endpoint;
		m.VFS_PM_EID = rmp->mp_effgid;
		m.VFS_PM_RID = rmp->mp_realgid;

		break;

	case PM_SETGROUPS:
		if (rmp->mp_effuid != SUPER_USER)
			return(EPERM);

		ngroups = m_in.m_lc_pm_groups.num;

		if (ngroups > NGROUPS_MAX || ngroups < 0)
			return(EINVAL);

		if (ngroups > 0 && m_in.m_lc_pm_groups.ptr == 0)
			return(EFAULT);

		r = sys_datacopy(who_e, m_in.m_lc_pm_groups.ptr, SELF,
			     (vir_bytes) rmp->mp_sgroups,
			     ngroups * sizeof(gid_t));
		if (r != OK)
			return(r);

		for (i = 0; i < ngroups; i++) {
			if (rmp->mp_sgroups[i] > GID_MAX)
				return(EINVAL);
		}
		for (i = ngroups; i < NGROUPS_MAX; i++) {
			rmp->mp_sgroups[i] = 0;
		}
		rmp->mp_ngroups = ngroups;

		m.m_type = VFS_PM_SETGROUPS;
		m.VFS_PM_ENDPT = rmp->mp_endpoint;
		m.VFS_PM_GROUP_NO = rmp->mp_ngroups;
		m.VFS_PM_GROUP_ADDR = (char *) rmp->mp_sgroups;

		break;
	case PM_SETSID:
		if (rmp->mp_procgrp == rmp->mp_pid) return(EPERM);
		rmp->mp_procgrp = rmp->mp_pid;

		m.m_type = VFS_PM_SETSID;
		m.VFS_PM_ENDPT = rmp->mp_endpoint;

		break;

	default:
		return(EINVAL);
  }

  /* Send the request to VFS */
  tell_vfs(rmp, &m);

  /* Do not reply until VFS has processed the request */
  return(SUSPEND);
}
