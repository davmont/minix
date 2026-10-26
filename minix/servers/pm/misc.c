/* Miscellaneous system calls.				Author: Kees J. Bot
 *								31 Mar 2000
 * The entry points into this file are:
 *   do_reboot: kill all processes, then reboot system
 *   do_getsysinfo: request copy of PM data structure  (Jorrit N. Herder)
 *   do_getprocnr: lookup endpoint by process ID
 *   do_getepinfo: get the pid/uid/gid of a process given its endpoint
 *   do_getsetpriority: get/set process priority
 *   do_svrctl: process manager control
 *   do_getrusage: obtain process resource usage information
 */

#include "pm.h"
#include <minix/callnr.h>
#include <signal.h>
#include <sys/svrctl.h>
#include <sys/reboot.h>
#include <sys/resource.h>
#include <sys/sched.h>
#include <sys/utsname.h>
#include <minix/com.h>
#include <minix/config.h>
#include <minix/sysinfo.h>
#include <minix/type.h>
#include <minix/ds.h>
#include <machine/archtypes.h>
#include <lib.h>
#include <assert.h>
#include "mproc.h"
#include "kernel/proc.h"

/* START OF COMPATIBILITY BLOCK */
struct utsname uts_val = {
  OS_NAME,		/* system name */
  "noname",		/* node/network name */
  OS_RELEASE,		/* O.S. release (e.g. 3.3.0) */
  OS_VERSION,		/* O.S. version (e.g. Minix 3.3.0 (GENERIC)) */
#if defined(__i386__)
  "i386",		/* machine (cpu) type */
#elif defined(__arm__)
  "evbarm",		/* machine (cpu) type */
#elif defined(__x86_64__) || defined(__amd64__)
  "x86_64",		/* machine (cpu) type */
#else
#error			/* oops, no 'uname -mk' */
#endif
};

static char *uts_tbl[] = {
#if defined(__i386__)
  "i386",		/* architecture */
#elif defined(__arm__)
  "evbarm",		/* architecture */
#elif defined(__x86_64__) || defined(__amd64__)
  "x86_64",		/* architecture */
#endif
  NULL,			/* No kernel architecture */
  uts_val.machine,
  NULL,			/* No hostname */
  uts_val.nodename,
  uts_val.release,
  uts_val.version,
  uts_val.sysname,
  NULL,			/* No bus */			/* No bus */
};
/* END OF COMPATIBILITY BLOCK */

#if ENABLE_SYSCALL_STATS
unsigned long calls_stats[NR_PM_CALLS];
#endif

/* START OF COMPATIBILITY BLOCK */
/*===========================================================================*
 *				do_sysuname				     *
 *===========================================================================*/
int
do_sysuname(void)
{
/* Set or get uname strings. */
  int r;
  size_t n;
  char *string;

  if (m_in.m_lc_pm_sysuname.field >= __arraycount(uts_tbl)) return(EINVAL);

  string = uts_tbl[m_in.m_lc_pm_sysuname.field];
  if (string == NULL)
	return EINVAL;	/* Unsupported field */

  switch (m_in.m_lc_pm_sysuname.req) {
  case 0:
	/* Copy an uname string to the user. */
	n = strlen(string) + 1;
	if (n > m_in.m_lc_pm_sysuname.len) n = m_in.m_lc_pm_sysuname.len;
	r = sys_datacopy(SELF, (vir_bytes)string, mp->mp_endpoint,
		m_in.m_lc_pm_sysuname.value, (phys_bytes)n);
	if (r < 0) return(r);
	break;

  default:
	return(EINVAL);
  }
  /* Return the number of bytes moved. */
  return(n);
}
/* END OF COMPATIBILITY BLOCK */


/*===========================================================================*
 *				do_sched_yield			       	     *
 *===========================================================================*/
int
do_sched_yield(void)
{
/* Perform sched_yield(2).  The scheduler treats it as the end of the
 * caller's quantum (see do_yield in SCHED), and the reply then queues the
 * caller at the tail of its new priority queue.
 */
  return(sched_yield_proc(mp));
}

static void inherit_update(struct mproc *rmp);

/*===========================================================================*
 *				sched_set_effective		       	     *
 *===========================================================================*/
static int
sched_set_effective(struct mproc *rmp, int policy, int prio)
{
/* Have 'rmp' run with its own 'policy' and 'prio', or higher: at the ceiling
 * of the PTHREAD_PRIO_PROTECT mutexes it holds, or at the priority of the
 * threads waiting for its PTHREAD_PRIO_INHERIT ones; as SCHED_FIFO if its own
 * policy is SCHED_OTHER.  Its own policy is the one sched_getscheduler(2)
 * reports.  The thread it waits for, if any, then inherits the result.
 */
  struct pm_inherit *pi = &mpinherit[rmp - mproc];
  int boost, level, r;

  boost = mpprotect[rmp - mproc].ceiling;
  if (pi->inherited > boost)
	boost = pi->inherited;
  if (boost >= 0 && (policy == SCHED_OTHER || boost > prio)) {
	if (policy == SCHED_OTHER)
		policy = SCHED_FIFO;
	prio = boost;
  }
  r = sched_set_policy(rmp, policy, prio);

  /* One that VFS has not given to its scheduler yet (EBADEPT, see
   * do_sched_param()) starts with it.
   */
  level = (policy == SCHED_OTHER) ? -1 : prio;
  if ((r == OK || r == EBADEPT) && level != pi->level) {
	pi->level = level;
	if (pi->on >= 0)
		inherit_update(&mproc[pi->on]);
  }
  return(r);
}

/*===========================================================================*
 *				do_sched_param			       	     *
 *===========================================================================*/
int
do_sched_param(void)
{
/* Perform PM_SCHED_GETPARAM and PM_SCHED_SETPARAM, for sched_getscheduler(2),
 * sched_setscheduler(2) and co. and pthread_[gs]etschedparam(3).  The target
 * is process 'pid' (0: the caller), or its thread 'lid' if not 0; a thread
 * has a slot of its own, so the policy is per thread.  Anyone may look.
 * Changing it takes what nice(3) does, and root for SCHED_FIFO and SCHED_RR:
 * those run ahead of every time-sharing process.  SET returns the previous
 * policy.
 */
  struct mproc *rmp;
  pid_t pid;
  int lid, policy, prio, old, r, proc_nr;

  pid = m_in.m_lc_pm_sched.pid;
  lid = m_in.m_lc_pm_sched.lid;
  if (pid < 0)
	return(EINVAL);
  if (pid == 0)
	rmp = mp;
  else if ((rmp = find_proc(pid)) == NULL)
	return(ESRCH);
  if (lid != 0) {
	if (pm_isokendpt(lid, &proc_nr) != OK ||
	    !(mproc[proc_nr].mp_flags & IN_USE) ||
	    process_of(&mproc[proc_nr]) != process_of(rmp))
		return(ESRCH);
	rmp = &mproc[proc_nr];
  }
  /* An exiting process has no scheduler any more, and may well have been
   * waited for already: its slot goes once VFS is done with it.
   */
  if (rmp->mp_flags & EXITING)
	return(ESRCH);

  if (call_nr == PM_SCHED_GETPARAM) {
	mp->mp_reply.m_lc_pm_sched.policy = rmp->mp_policy;
	mp->mp_reply.m_lc_pm_sched.prio = rmp->mp_rtprio;
	return(OK);
  }

  policy = m_in.m_lc_pm_sched.policy;
  prio = m_in.m_lc_pm_sched.prio;
  if (policy == SCHED_NONE)
	policy = rmp->mp_policy;
  switch (policy) {
  case SCHED_OTHER:
	if (prio != 0)
		return(EINVAL);
	break;
  case SCHED_FIFO:
  case SCHED_RR:
	if (prio < RT_PRIO_MIN || prio > RT_PRIO_MAX)
		return(EINVAL);
	break;
  default:
	return(EINVAL);
  }

  if (mp->mp_effuid != SUPER_USER &&
     mp->mp_effuid != rmp->mp_effuid && mp->mp_effuid != rmp->mp_realuid)
	return(EPERM);
  if (policy != SCHED_OTHER && mp->mp_effuid != SUPER_USER)
	return(EPERM);

  /* System services have a band of their own and keep it.  A new process or
   * thread is in its scheduler only once VFS has it (VFS_PM_FORK_REPLY,
   * VFS_PM_LWP_REPLY), and pthread_create() sets the policy of the thread
   * before: the scheduler does not know it yet (EBADEPT), and is to start
   * it with what is set here.
   */
  r = sched_set_effective(rmp, policy, prio);
  if (r == EBADEPT && (rmp->mp_flags & VFS_CALL) &&
      !(rmp->mp_flags & MP_LWP_TEARDOWN))
	r = OK;
  if (r != OK)
	return(r);

  old = rmp->mp_policy;
  rmp->mp_policy = policy;
  rmp->mp_rtprio = prio;
  return(old);
}

/*===========================================================================*
 *				do_sched_protect		       	     *
 *===========================================================================*/
int
do_sched_protect(void)
{
/* Perform _sched_protect(2), which libpthread calls when the caller locks a
 * PTHREAD_PRIO_PROTECT mutex, with its ceiling, and with -1 when it unlocks
 * one.  The thread runs at the highest ceiling of those it holds until it
 * holds none, as on NetBSD.  A ceiling below the caller's own real-time
 * priority is EINVAL (POSIX: pthread_mutex_lock(3)).  Only root is raised:
 * real-time priority is root's, as for sched_setscheduler(2), and a mutex is
 * no way around that.  Anyone else still gets the lock, at its own priority.
 */
  struct pm_protect *pp = &mpprotect[mp - mproc];
  int prio, old;

  prio = m_in.m_lc_pm_sched.prio;
  if (prio == -1) {
	if (pp->depth == 0)
		return(OK);		/* say, a mutex locked before fork() */
	if (--pp->depth > 0 || pp->ceiling < 0)
		return(OK);
	pp->ceiling = -1;
	return(sched_set_effective(mp, mp->mp_policy, mp->mp_rtprio));
  }

  if (prio < RT_PRIO_MIN || prio > RT_PRIO_MAX)
	return(EINVAL);
  if (mp->mp_policy != SCHED_OTHER && mp->mp_rtprio > prio)
	return(EINVAL);
  pp->depth++;
  if (mp->mp_effuid != SUPER_USER || prio <= pp->ceiling)
	return(OK);

  old = pp->ceiling;
  pp->ceiling = prio;
  if (sched_set_effective(mp, mp->mp_policy, mp->mp_rtprio) != OK)
	pp->ceiling = old;	/* a system service: it keeps its band */
  return(OK);
}

/*===========================================================================*
 *				inherit_update		       	     	     *
 *===========================================================================*/
static void
inherit_update(struct mproc *rmp)
{
/* The threads waiting for 'rmp' have changed, or their priorities: have it
 * run at the highest of these.  That goes on down a chain of threads each
 * waiting for the next, as far as a cycle (a deadlock) lets it.
 */
  static int depth;
  struct mproc *t;
  int slot = (int)(rmp - mproc), inherited = -1;

  for (t = &mproc[0]; t < &mproc[NR_PROCS]; t++) {
	if ((t->mp_flags & IN_USE) && mpinherit[t - mproc].on == slot &&
	    mpinherit[t - mproc].level > inherited)
		inherited = mpinherit[t - mproc].level;
  }
  if (inherited == mpinherit[slot].inherited || depth >= INHERIT_DEPTH)
	return;
  mpinherit[slot].inherited = inherited;
  depth++;
  (void) sched_set_effective(rmp, rmp->mp_policy, rmp->mp_rtprio);
  depth--;
}

/*===========================================================================*
 *				inherit_stop		       	     	     *
 *===========================================================================*/
static void
inherit_stop(struct mproc *rmp)
{
/* 'rmp' no longer waits for a mutex. */
  struct pm_inherit *pi = &mpinherit[rmp - mproc];
  int on = pi->on;

  if (on < 0)
	return;
  pi->on = -1;
  inherit_update(&mproc[on]);
}

/*===========================================================================*
 *				do_sched_inherit		       	     *
 *===========================================================================*/
int
do_sched_inherit(void)
{
/* Perform _sched_inherit(2), which libpthread calls for PTHREAD_PRIO_INHERIT
 * mutexes, identified by the address of their owner word: WAIT before the
 * caller parks on one, which thread 'lid' holds, DONE once it is awake, and
 * RELEASE when the caller unlocks one.  A thread runs at the highest
 * priority of those waiting for it (POSIX), as long as they wait for it,
 * which a thread in the same process may well have without being root.
 *
 * The owner may have unlocked the mutex, and asked for RELEASE, since the
 * caller saw it as the owner: PM asks for each call in turn, so if the word
 * still says what the caller saw, the RELEASE is still to come.  If not, the
 * WAIT is EAGAIN.
 */
  struct pm_inherit *pi = &mpinherit[mp - mproc];
  struct mproc *owner, *t;
  vir_bytes key, word;
  int proc_nr, r;

  key = m_in.m_lc_pm_inherit.key;
  switch (m_in.m_lc_pm_inherit.op) {
  case SCHED_INHERIT_WAIT:
	if (pm_isokendpt(m_in.m_lc_pm_inherit.lid, &proc_nr) != OK)
		return(ESRCH);
	owner = &mproc[proc_nr];
	if (owner == mp || (owner->mp_flags & (IN_USE | EXITING |
	    MP_LWP_TEARDOWN)) != IN_USE || process_of(owner) != process_of(mp))
		return(ESRCH);
	if ((r = sys_datacopy(who_e, key, PM_PROC_NR, (vir_bytes) &word,
	    sizeof(word))) != OK)
		return(r);
	if (word != m_in.m_lc_pm_inherit.owner)
		return(EAGAIN);
	inherit_stop(mp);
	pi->on = proc_nr;
	pi->key = key;
	inherit_update(owner);
	return(OK);

  case SCHED_INHERIT_DONE:
	inherit_stop(mp);
	return(OK);

  case SCHED_INHERIT_RELEASE:
	/* Its waiters are awake, or about to be; whichever of them does not
	 * get it waits again, for the next owner.
	 */
	for (t = &mproc[0]; t < &mproc[NR_PROCS]; t++) {
		if ((t->mp_flags & IN_USE) &&
		    mpinherit[t - mproc].on == (int)(mp - mproc) &&
		    mpinherit[t - mproc].key == key)
			mpinherit[t - mproc].on = -1;
	}
	inherit_update(mp);
	return(OK);

  default:
	return(EINVAL);
  }
}

/*===========================================================================*
 *				sched_inherit_init		       	     *
 *===========================================================================*/
void
sched_inherit_init(struct mproc *rmp)
{
/* 'rmp' is new, or starts over (exec), with its own policy: it waits for no
 * mutex, and no thread waits for it.
 */
  struct pm_inherit *pi = &mpinherit[rmp - mproc];

  pi->on = -1;
  pi->inherited = -1;
  pi->level = (rmp->mp_policy == SCHED_OTHER) ? -1 : rmp->mp_rtprio;
}

/*===========================================================================*
 *				sched_inherit_exit		       	     *
 *===========================================================================*/
void
sched_inherit_exit(struct mproc *rmp)
{
/* 'rmp' is going: those waiting for it wait for no one, and the one it waits
 * for, if any, no longer inherits its priority.
 */
  struct mproc *t;
  int slot = (int)(rmp - mproc);

  inherit_stop(rmp);
  for (t = &mproc[0]; t < &mproc[NR_PROCS]; t++)
	if (mpinherit[t - mproc].on == slot)
		mpinherit[t - mproc].on = -1;
  mpinherit[slot].inherited = -1;
}

/*===========================================================================*
 *				set_affinity			       	     *
 *===========================================================================*/
static int
set_affinity(struct mproc *rmp, uint32_t mask)
{
/* Have the thread 'rmp' run on the CPUs in 'mask' (0: any).  One that VFS
 * has not given to its scheduler yet starts with it (see do_sched_param()).
 */
  int r;

  r = sched_set_affinity(rmp, mask);
  if (r == EBADEPT && (rmp->mp_flags & VFS_CALL) &&
      !(rmp->mp_flags & MP_LWP_TEARDOWN))
	r = OK;
  if (r != OK)
	return(r);
  mpcpumask[rmp - mproc] = mask;
  return(OK);
}

/*===========================================================================*
 *				do_sched_affinity		       	     *
 *===========================================================================*/
int
do_sched_affinity(void)
{
/* Perform PM_SCHED_AFFINITY, for _sched_setaffinity(2) and
 * _sched_getaffinity(2): the CPUs that thread 'lid' of process 'pid' (0: the
 * caller) may run on.  With lid 0, setting is for all its threads, and
 * getting is for the calling thread if 'pid' is its own process, else for its
 * first thread.  The set is a mask: 0, or every CPU there is, is any CPU; one
 * with none of them is EINVAL.  Anyone may look; changing it takes what
 * nice(3) does.
 */
  struct mproc *rmp, *t;
  pid_t pid;
  int lid, proc_nr, r;
  uint32_t mask, all;

  all = (machine.processors_count >= 32) ? ~0U :
	(1U << machine.processors_count) - 1;
  pid = m_in.m_lc_pm_affinity.pid;
  lid = m_in.m_lc_pm_affinity.lid;
  if (pid < 0)
	return(EINVAL);
  if (pid == 0)
	rmp = mp;
  else if ((rmp = find_proc(pid)) == NULL)
	return(ESRCH);
  if (lid != 0) {
	if (pm_isokendpt(lid, &proc_nr) != OK ||
	    !(mproc[proc_nr].mp_flags & IN_USE) ||
	    process_of(&mproc[proc_nr]) != process_of(rmp))
		return(ESRCH);
	rmp = &mproc[proc_nr];
  } else if (process_of(rmp) == process_of(mp))
	rmp = mp;
  else
	rmp = process_of(rmp);
  if (rmp->mp_flags & EXITING)
	return(ESRCH);

  if (!m_in.m_lc_pm_affinity.set) {
	mask = mpcpumask[rmp - mproc];
	mp->mp_reply.m_lc_pm_affinity.cpumask = (mask != 0) ? mask : all;
	return(OK);
  }

  mask = m_in.m_lc_pm_affinity.cpumask;
  if (mask != 0 && (mask & all) == 0)
	return(EINVAL);
  mask &= all;
  if (mask == all)
	mask = 0;

  if (mp->mp_effuid != SUPER_USER &&
     mp->mp_effuid != rmp->mp_effuid && mp->mp_effuid != rmp->mp_realuid)
	return(EPERM);

  if (lid != 0)
	return(set_affinity(rmp, mask));
  for (t = &mproc[0]; t < &mproc[NR_PROCS]; t++) {
	if ((t->mp_flags & (IN_USE | EXITING | MP_LWP_ZOMBIE)) != IN_USE ||
	    process_of(t) != process_of(rmp))
		continue;
	if ((r = set_affinity(t, mask)) != OK)
		return(r);
  }
  return(OK);
}

/*===========================================================================*
 *				sched_protect_reset		       	     *
 *===========================================================================*/
void
sched_protect_reset(struct mproc *rmp)
{
/* 'rmp' holds no PTHREAD_PRIO_PROTECT or PTHREAD_PRIO_INHERIT mutex any more
 * (exec): back to its own priority.
 */
  struct pm_protect *pp = &mpprotect[rmp - mproc];
  int boosted;

  boosted = (pp->ceiling >= 0 || mpinherit[rmp - mproc].inherited >= 0);
  pp->depth = 0;
  pp->ceiling = -1;
  sched_inherit_exit(rmp);
  if (boosted)
	(void) sched_set_effective(rmp, rmp->mp_policy, rmp->mp_rtprio);
}

/*===========================================================================*
 *				do_getsysinfo			       	     *
 *===========================================================================*/
int
do_getsysinfo(void)
{
  vir_bytes src_addr, dst_addr;
  size_t len;

  /* This call leaks important information. In the future, requests from
   * non-system processes should be denied.
   */
  if (mp->mp_effuid != 0)
  {
	printf("PM: unauthorized call of do_getsysinfo by proc %d '%s'\n",
		mp->mp_endpoint, mp->mp_name);
	sys_diagctl_stacktrace(mp->mp_endpoint);
	return EPERM;
  }

  switch(m_in.m_lsys_getsysinfo.what) {
  case SI_PROC_TAB:			/* copy entire process table */
        src_addr = (vir_bytes) mproc;
        len = sizeof(struct mproc) * NR_PROCS;
        break;
#if ENABLE_SYSCALL_STATS
  case SI_CALL_STATS:
  	src_addr = (vir_bytes) calls_stats;
  	len = sizeof(calls_stats);
  	break;
#endif
  default:
  	return(EINVAL);
  }

  if (len != m_in.m_lsys_getsysinfo.size)
	return(EINVAL);

  dst_addr = m_in.m_lsys_getsysinfo.where;
  return sys_datacopy(SELF, src_addr, who_e, dst_addr, len);
}

/*===========================================================================*
 *				do_getprocnr			             *
 *===========================================================================*/
int do_getprocnr(void)
{
  register struct mproc *rmp;

  /* This check should be replaced by per-call ACL checks. */
  if (who_e != RS_PROC_NR) {
	printf("PM: unauthorized call of do_getprocnr by %d\n", who_e);
	return EPERM;
  }

  if ((rmp = find_proc(m_in.m_lsys_pm_getprocnr.pid)) == NULL)
	return(ESRCH);

  mp->mp_reply.m_pm_lsys_getprocnr.endpt = rmp->mp_endpoint;
  return(OK);
}

/*===========================================================================*
 *				do_getepinfo			             *
 *===========================================================================*/
int do_getepinfo(void)
{
  struct mproc *rmp;
  endpoint_t ep;
  int r, slot, ngroups;

  ep = m_in.m_lsys_pm_getepinfo.endpt;
  if (pm_isokendpt(ep, &slot) != OK)
	return(ESRCH);
  rmp = &mproc[slot];

  mp->mp_reply.m_pm_lsys_getepinfo.uid = rmp->mp_realuid;
  mp->mp_reply.m_pm_lsys_getepinfo.euid = rmp->mp_effuid;
  mp->mp_reply.m_pm_lsys_getepinfo.gid = rmp->mp_realgid;
  mp->mp_reply.m_pm_lsys_getepinfo.egid = rmp->mp_effgid;
  mp->mp_reply.m_pm_lsys_getepinfo.ngroups = ngroups = rmp->mp_ngroups;
  if (ngroups > m_in.m_lsys_pm_getepinfo.ngroups)
	ngroups = m_in.m_lsys_pm_getepinfo.ngroups;
  if (ngroups > 0) {
	if ((r = sys_datacopy(SELF, (vir_bytes)rmp->mp_sgroups, who_e,
	    m_in.m_lsys_pm_getepinfo.groups, ngroups * sizeof(gid_t))) != OK)
		return(r);
  }
  return(rmp->mp_pid);
}

/*===========================================================================*
 *				do_reboot				     *
 *===========================================================================*/
int
do_reboot(void)
{
  message m;

  /* Check permission to abort the system. */
  if (mp->mp_effuid != SUPER_USER) return(EPERM);

  /* See how the system should be aborted. */
  abort_flag = m_in.m_lc_pm_reboot.how;

  /* notify readclock (some arm systems power off via RTC alarms) */
  if (abort_flag & RB_POWERDOWN) {
	endpoint_t readclock_ep;
	if (ds_retrieve_label_endpt("readclock.drv", &readclock_ep) == OK) {
		message m; /* no params to set, nothing we can do if it fails */
		_taskcall(readclock_ep, RTCDEV_PWR_OFF, &m);
	}
  }

  /* Order matters here. When VFS is told to reboot, it exits all its
   * processes, and then would be confused if they're exited again by
   * SIGKILL. So first kill, then reboot.
   */

  check_sig(-1, SIGKILL, FALSE /* ksig*/); /* kill all users except init */
  sys_stop(INIT_PROC_NR);		   /* stop init, but keep it around */

  /* Tell VFS to reboot */
  memset(&m, 0, sizeof(m));
  m.m_type = VFS_PM_REBOOT;

  tell_vfs(&mproc[VFS_PROC_NR], &m);

  return(SUSPEND);			/* don't reply to caller */
}

/*===========================================================================*
 *				set_nice				     *
 *===========================================================================*/
static int
set_nice(struct mproc *rmp, int nice)
{
/* Give thread slot 'rmp' the nice level 'nice'.  One the scheduler does not
 * have yet (EBADEPT while VFS has it, see do_sched_param) starts with it; one
 * on its way out, or a leader whose own thread is gone, just keeps the value.
 */
	int r;

	if (!(rmp->mp_flags & (EXITING | MP_LWP_TEARDOWN | MP_LEADER_GONE))) {
		r = sched_nice(rmp, nice);
		if (r == EBADEPT && (rmp->mp_flags & VFS_CALL))
			r = OK;
		if (r != OK)
			return(r);
	}
	rmp->mp_nice = nice;
	return(OK);
}

/*===========================================================================*
 *				do_getsetpriority			     *
 *===========================================================================*/
int
do_getsetpriority(void)
{
/* The nice level is the process's (POSIX): every thread of it has it, each
 * thread being a slot of its own, and its leader's slot stands for it.
 */
	int r, arg_which, arg_who, arg_pri, slot;
	struct mproc *rmp, *t;

	arg_which = m_in.m_lc_pm_priority.which;
	arg_who = m_in.m_lc_pm_priority.who;
	arg_pri = m_in.m_lc_pm_priority.prio;	/* for SETPRIORITY */

	/* Code common to GETPRIORITY and SETPRIORITY. */

	/* Only support PRIO_PROCESS for now. */
	if (arg_which != PRIO_PROCESS)
		return(EINVAL);

	if (arg_who == 0)
		rmp = mp;
	else
		if ((rmp = find_proc(arg_who)) == NULL)
			return(ESRCH);
	rmp = process_of(rmp);

	if (mp->mp_effuid != SUPER_USER &&
	   mp->mp_effuid != rmp->mp_effuid && mp->mp_effuid != rmp->mp_realuid)
		return EPERM;

	/* If GET, that's it. */
	if (call_nr == PM_GETPRIORITY) {
		return(rmp->mp_nice - PRIO_MIN);
	}

	/* Only root is allowed to reduce the nice level. */
	if (rmp->mp_nice > arg_pri && mp->mp_effuid != SUPER_USER)
		return(EACCES);

	/* We're SET, and it's allowed.
	 *
	 * The value passed in is currently between PRIO_MIN and PRIO_MAX.
	 * We have to scale this between MIN_USER_Q and MAX_USER_Q to match
	 * the kernel's scheduling queues.
	 */

	if ((r = set_nice(rmp, arg_pri)) != OK)
		return(r);
	slot = (int)(rmp - mproc);
	for (t = &mproc[0]; t < &mproc[NR_PROCS]; t++) {
		if ((t->mp_flags & (IN_USE | MP_LWP)) == (IN_USE | MP_LWP) &&
		    t->mp_lwp_group == slot)
			(void)set_nice(t, arg_pri);
	}
	return(OK);
}

/*===========================================================================*
 *				do_svrctl				     *
 *===========================================================================*/
int do_svrctl(void)
{
  unsigned long req;
  int s;
  vir_bytes ptr;
#define MAX_LOCAL_PARAMS 2
  static struct {
  	char name[30];
  	char value[30];
  } local_param_overrides[MAX_LOCAL_PARAMS];
  static int local_params = 0;

  req = m_in.m_lc_svrctl.request;
  ptr = m_in.m_lc_svrctl.arg;

  /* Is the request indeed for the PM? ('M' is old and being phased out) */
  if (IOCGROUP(req) != 'P' && IOCGROUP(req) != 'M') return(EINVAL);

  /* Control operations local to the PM. */
  switch(req) {
  case OPMSETPARAM:
  case OPMGETPARAM:
  case PMSETPARAM:
  case PMGETPARAM: {
      struct sysgetenv sysgetenv;
      char search_key[64];
      char *val_start;
      size_t val_len;
      size_t copy_len;

      /* Copy sysgetenv structure to PM. */
      if (sys_datacopy(who_e, ptr, SELF, (vir_bytes) &sysgetenv,
              sizeof(sysgetenv)) != OK) return(EFAULT);

      /* Set a param override? */
      if (req == PMSETPARAM || req == OPMSETPARAM) {
  	if (local_params >= MAX_LOCAL_PARAMS) return ENOSPC;
  	if (sysgetenv.keylen <= 0
  	 || sysgetenv.keylen >=
  	 	 sizeof(local_param_overrides[local_params].name)
  	 || sysgetenv.vallen <= 0
  	 || sysgetenv.vallen >=
  	 	 sizeof(local_param_overrides[local_params].value))
  		return EINVAL;

          if ((s = sys_datacopy(who_e, (vir_bytes) sysgetenv.key,
            SELF, (vir_bytes) local_param_overrides[local_params].name,
               sysgetenv.keylen)) != OK)
               	return s;
          if ((s = sys_datacopy(who_e, (vir_bytes) sysgetenv.val,
            SELF, (vir_bytes) local_param_overrides[local_params].value,
              sysgetenv.vallen)) != OK)
               	return s;
            local_param_overrides[local_params].name[sysgetenv.keylen] = '\0';
            local_param_overrides[local_params].value[sysgetenv.vallen] = '\0';

  	local_params++;

  	return OK;
      }

      if (sysgetenv.keylen == 0) {	/* copy all parameters */
          val_start = monitor_params;
          val_len = sizeof(monitor_params);
      }
      else {				/* lookup value for key */
      	  int p;
          /* Try to get a copy of the requested key. */
          if (sysgetenv.keylen > sizeof(search_key)) return(EINVAL);
          if ((s = sys_datacopy(who_e, (vir_bytes) sysgetenv.key,
                  SELF, (vir_bytes) search_key, sysgetenv.keylen)) != OK)
              return(s);

          /* Make sure key is null-terminated and lookup value.
           * First check local overrides.
           */
          search_key[sysgetenv.keylen-1]= '\0';
          for(p = 0; p < local_params; p++) {
          	if (!strcmp(search_key, local_param_overrides[p].name)) {
          		val_start = local_param_overrides[p].value;
          		break;
          	}
          }
          if (p >= local_params && (val_start = find_param(search_key)) == NULL)
               return(ESRCH);
          val_len = strlen(val_start) + 1;
      }

      /* See if it fits in the client's buffer. */
      if (val_len > sysgetenv.vallen)
      	return E2BIG;

      /* Value found, make the actual copy (as far as possible). */
      copy_len = MIN(val_len, sysgetenv.vallen);
      if ((s=sys_datacopy(SELF, (vir_bytes) val_start,
              who_e, (vir_bytes) sysgetenv.val, copy_len)) != OK)
          return(s);

      return OK;
  }

  default:
	return(EINVAL);
  }
}

/*===========================================================================*
 *				do_getrusage				     *
 *===========================================================================*/
int
do_getrusage(void)
{
	clock_t user_time, sys_time;
	struct rusage r_usage;
	int r, children;

	if (m_in.m_lc_pm_rusage.who != RUSAGE_SELF &&
	    m_in.m_lc_pm_rusage.who != RUSAGE_CHILDREN)
		return EINVAL;

	/*
	 * TODO: first relay the call to VFS.  As is, VFS does not have any
	 * fields it can fill with meaningful values, but this may change in
	 * the future.  In that case, PM would first have to use the tell_vfs()
	 * system to get those values from VFS, and do the rest here upon
	 * getting the response.
	 */

	memset(&r_usage, 0, sizeof(r_usage));

	children = (m_in.m_lc_pm_rusage.who == RUSAGE_CHILDREN);

	/*
	 * Get system times.  For RUSAGE_SELF, get the times for the calling
	 * process from the kernel.  For RUSAGE_CHILDREN, we already have the
	 * values we should return right here.
	 */
	if (!children) {
		if ((r = sys_times(who_e, &user_time, &sys_time, NULL,
		    NULL)) != OK)
			return r;
	} else {
		user_time = mp->mp_child_utime;
		sys_time = mp->mp_child_stime;
	}

	/* In both cases, convert from clock ticks to microseconds. */
	set_rusage_times(&r_usage, user_time, sys_time);

	/* Get additional fields from VM. */
	if ((r = vm_getrusage(who_e, &r_usage, children)) != OK)
		return r;

	/* Finally copy the structure to the caller. */
	return sys_datacopy(SELF, (vir_bytes)&r_usage, who_e,
	    m_in.m_lc_pm_rusage.addr, (vir_bytes)sizeof(r_usage));
}
