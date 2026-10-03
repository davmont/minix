/* This file takes care of those system calls that deal with time.
 *
 * The entry points into this file are
 *   do_getres:		perform the CLOCK_GETRES system call
 *   do_gettime:	perform the CLOCK_GETTIME system call
 *   do_settime:	perform the CLOCK_SETTIME system call
 *   do_time:		perform the GETTIMEOFDAY system call
 *   do_stime:		perform the STIME system call
 *
 * Besides CLOCK_REALTIME and CLOCK_MONOTONIC, clock_gettime(2) knows the CPU
 * time clocks: CLOCK_PROCESS_CPUTIME_ID and CLOCK_THREAD_CPUTIME_ID, alone
 * for the caller, or with a pid (clock_getcpuclockid(3)) or a thread's lwpid
 * (pthread_getcpuclockid(3)) in the low bits.
 */

#include "pm.h"
#include <minix/callnr.h>
#include <minix/com.h>
#include <signal.h>
#include <sys/time.h>
#include "mproc.h"

#define CPUCLOCK_ID_MASK	0x1fffffff

/* Add the CPU time 'rmp' has used to '*ticks'. */
static void
add_cpu_time(struct mproc *rmp, clock_t *ticks)
{
  clock_t user_time, sys_time;

  if (sys_times(rmp->mp_endpoint, &user_time, &sys_time, NULL, NULL) == OK)
	*ticks += user_time + sys_time;
}

/* The CPU time, in ticks, that the CPU time clock 'clk' shows. */
static int
cpu_clock(clockid_t clk, clock_t *ticks)
{
  struct mproc *rmp, *t;
  int id, leader;

  id = clk & CPUCLOCK_ID_MASK;
  *ticks = 0;

  switch (clk & ~CPUCLOCK_ID_MASK) {
  case CLOCK_PROCESS_CPUTIME_ID:
	if (id == 0)
		rmp = mp;
	else if ((rmp = find_proc(id)) == NULL)
		return EINVAL;
	/* All threads of the process, and the ones that have exited. */
	leader = (rmp->mp_flags & MP_LWP) ? rmp->mp_lwp_group :
		(int) (rmp - mproc);
	*ticks = mproc[leader].mp_lwp_time;
	for (t = &mproc[0]; t < &mproc[NR_PROCS]; t++) {
		if (!(t->mp_flags & IN_USE))
			continue;
		if ((int) (t - mproc) == leader ||
		    ((t->mp_flags & MP_LWP) && t->mp_lwp_group == leader))
			add_cpu_time(t, ticks);
	}
	return OK;

  case CLOCK_THREAD_CPUTIME_ID:
	if (id == 0) {
		add_cpu_time(mp, ticks);
		return OK;
	}
	/* A thread of the caller's process, by its lwpid (endpoint). */
	leader = (mp->mp_flags & MP_LWP) ? mp->mp_lwp_group :
		(int) (mp - mproc);
	for (t = &mproc[0]; t < &mproc[NR_PROCS]; t++) {
		if (!(t->mp_flags & IN_USE) ||
		    (t->mp_endpoint & CPUCLOCK_ID_MASK) != id)
			continue;
		if ((int) (t - mproc) == leader ||
		    ((t->mp_flags & MP_LWP) && t->mp_lwp_group == leader)) {
			add_cpu_time(t, ticks);
			return OK;
		}
	}
	return EINVAL;

  default:
	return EINVAL;
  }
}

/*===========================================================================*
 *				do_gettime				     *
 *===========================================================================*/
int
do_gettime(void)
{
  clock_t ticks, realtime, clock;
  time_t boottime;
  int s;

  if ( (s=getuptime(&ticks, &realtime, &boottime)) != OK)
  	panic("do_time couldn't get uptime: %d", s);

  switch (m_in.m_lc_pm_time.clk_id) {
	case CLOCK_REALTIME:
		clock = realtime;
		break;
	case CLOCK_MONOTONIC:
		clock = ticks;
		break;
	default:
		/* A CPU time clock counts from zero, not from the boot. */
		if ((s = cpu_clock(m_in.m_lc_pm_time.clk_id, &clock)) != OK)
			return s;
		boottime = 0;
		break;
  }

  mp->mp_reply.m_pm_lc_time.sec = boottime + (clock / system_hz);
  mp->mp_reply.m_pm_lc_time.nsec =
	(uint32_t) ((clock % system_hz) * 1000000000ULL / system_hz);

  return(OK);
}

/*===========================================================================*
 *				do_getres				     *
 *===========================================================================*/
int
do_getres(void)
{
  switch (m_in.m_lc_pm_time.clk_id) {
	case CLOCK_REALTIME:
	case CLOCK_MONOTONIC:
		/* tv_sec is always 0 since system_hz is an int */
		mp->mp_reply.m_pm_lc_time.sec = 0;
		mp->mp_reply.m_pm_lc_time.nsec = 1000000000 / system_hz;
		return(OK);
	default: {
		/* The CPU time clocks count ticks too. */
		clock_t ticks;
		int r;

		if ((r = cpu_clock(m_in.m_lc_pm_time.clk_id, &ticks)) != OK)
			return r;
		mp->mp_reply.m_pm_lc_time.sec = 0;
		mp->mp_reply.m_pm_lc_time.nsec = 1000000000 / system_hz;
		return(OK);
	}
  }
}

/*===========================================================================*
 *				do_settime				     *
 *===========================================================================*/
int
do_settime(void)
{
  int s;

  if (mp->mp_effuid != SUPER_USER) {
      return(EPERM);
  }

  switch (m_in.m_lc_pm_time.clk_id) {
	case CLOCK_REALTIME:
		s = sys_settime(m_in.m_lc_pm_time.now, m_in.m_lc_pm_time.clk_id,
			m_in.m_lc_pm_time.sec, m_in.m_lc_pm_time.nsec);
		return(s);
	case CLOCK_MONOTONIC: /* monotonic cannot be changed */
	default:
		return EINVAL; /* invalid/unsupported clock_id */
  }
}

/*===========================================================================*
 *				do_time					     *
 *===========================================================================*/
int
do_time(void)
{
/* Perform the time(tp) system call. */
  struct timespec tv;

  (void)clock_time(&tv);

  mp->mp_reply.m_pm_lc_time.sec = tv.tv_sec;
  mp->mp_reply.m_pm_lc_time.nsec = tv.tv_nsec;
  return(OK);
}

/*===========================================================================*
 *				do_stime				     *
 *===========================================================================*/
int
do_stime(void)
{
/* Perform the stime(tp) system call. Retrieve the system's uptime (ticks
 * since boot) and pass the new time in seconds at system boot to the kernel.
 */
  clock_t uptime, realtime;
  time_t boottime;
  int s;

  if (mp->mp_effuid != SUPER_USER) {
      return(EPERM);
  }
  if ( (s=getuptime(&uptime, &realtime, &boottime)) != OK)
      panic("do_stime couldn't get uptime: %d", s);
  boottime = m_in.m_lc_pm_time.sec - (realtime/system_hz);

  s= sys_stime(boottime);		/* Tell kernel about boottime */
  if (s != OK)
	panic("pm: sys_stime failed: %d", s);

  return(OK);
}
