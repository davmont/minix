/* POSIX per-process timers: timer_create(2), timer_settime(2) and co.
 *
 * A timer keeps its expiry and interval to the nanosecond, on the monotonic
 * clock, and goes off at the first clock tick at or after its expiry (no
 * sooner: see timeout_ticks(3)).  It then raises the signal its sigevent
 * names, with si_code SI_TIMER and the sigev_value.  While that signal is
 * still pending, further expiries, however many periods went by meanwhile
 * (and an interval may be shorter than a tick), are counted as overruns
 * instead, and the count at the delivery of the signal is what
 * timer_getoverrun(2) reports.  Timers belong
 * to the process (the thread group), are not inherited across fork(2), and
 * are deleted by exec(2) and exit.
 *
 * The entry points into this file are:
 *   do_timer:		perform the PM_TIMER system call
 *   ptimer_delivered:	a timer's signal is being delivered
 *   ptimer_release:	delete a process's timers (exec, exit)
 */

#include "pm.h"
#include <signal.h>
#include <time.h>
#include <limits.h>
#include <string.h>
#include <minix/com.h>
#include <minix/callnr.h>
#include "mproc.h"

#define NR_PTIMERS	1024		/* timers in the system */
#define PTIMER_MAX	32		/* per process: _POSIX_TIMER_MAX */
#define DELAYTIMER_MAX	INT_MAX		/* overruns counted at most */

struct ptimer {
  int pt_inuse;
  int pt_owner;			/* mproc slot of the process (group leader) */
  endpoint_t pt_endpt;		/* its endpoint, to catch a reused slot */
  int pt_id;			/* the timer_t, unique within the process */
  clockid_t pt_clock;
  int pt_notify;		/* SIGEV_NONE or SIGEV_SIGNAL */
  int pt_signo;
  vir_bytes pt_value;		/* sigev_value */
  u64_t pt_expires;		/* monotonic time, ns; 0: disarmed */
  u64_t pt_interval;		/* ns; 0: one-shot */
  int pt_queued;		/* our signal raised and not yet delivered */
  int pt_overrun;		/* expiries while it was queued */
  int pt_overrun_last;		/* reported by timer_getoverrun(2) */
  minix_timer_t pt_tmr;
};

static struct ptimer ptimers[NR_PTIMERS];

static void ptimer_expire(int arg);

/* The slot that owns the process-wide state of 'rmp' (its group leader). */
static int
owner_slot(struct mproc *rmp)
{

  if (rmp->mp_lwp_group != NO_LWP_GROUP)
	return rmp->mp_lwp_group;
  return (int) (rmp - mproc);
}

static struct ptimer *
find_timer(int owner, int id)
{
  struct ptimer *pt;

  for (pt = &ptimers[0]; pt < &ptimers[NR_PTIMERS]; pt++)
	if (pt->pt_inuse && pt->pt_owner == owner && pt->pt_id == id)
		return pt;
  return NULL;
}

#define NSEC		1000000000ULL
#define FAR_NS		(NSEC * 60 * 60 * 24 * 365 * 100)	/* 100 years */

/* Nanoseconds in 'ts', at most FAR_NS: that is far enough. */
static int
ns_from_timespec(const struct timespec *ts, u64_t *ns)
{

  if (ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000L)
	return EINVAL;
  if ((u64_t) ts->tv_sec >= FAR_NS / NSEC)
	*ns = FAR_NS;
  else
	*ns = (u64_t) ts->tv_sec * NSEC + (u64_t) ts->tv_nsec;
  return OK;
}

static void
timespec_from_ns(struct timespec *ts, u64_t ns)
{

  ts->tv_sec = (time_t) (ns / NSEC);
  ts->tv_nsec = (long) (ns % NSEC);
}

/* Arm the watchdog for the timer's expiry.  The timer library takes at most
 * TMRDIFF_MAX ticks at a time; ptimer_expire() re-arms for the rest.
 */
static void
arm(struct ptimer *pt)
{

  set_timer(&pt->pt_tmr, deadline_ticks(pt->pt_expires), ptimer_expire,
	(int) (pt - ptimers));
}

/* Move the expiry of 'pt' past 'now' by whole periods, and say how many
 * went by entirely.
 */
static u64_t
skip_periods(struct ptimer *pt, u64_t now)
{
  u64_t missed;

  if (pt->pt_interval == 0 || pt->pt_expires > now)
	return 0;
  missed = (now - pt->pt_expires) / pt->pt_interval + 1;
  pt->pt_expires += missed * pt->pt_interval;
  return missed;
}

static void
add_overruns(struct ptimer *pt, u64_t n)
{

  if ((u64_t) pt->pt_overrun + n > DELAYTIMER_MAX)
	pt->pt_overrun = DELAYTIMER_MAX;
  else
	pt->pt_overrun += (int) n;
}

static void
disarm(struct ptimer *pt)
{

  if (pt->pt_expires != 0)
	cancel_timer(&pt->pt_tmr);
  pt->pt_expires = 0;
  pt->pt_interval = 0;
}

/* Is 'signo' pending anywhere in the process that 'owner' leads? */
static int
pending_in_process(int owner, int signo)
{
  struct mproc *rmp;

  for (rmp = &mproc[0]; rmp < &mproc[NR_PROCS]; rmp++) {
	if (!(rmp->mp_flags & IN_USE))
		continue;
	if ((int) (rmp - mproc) != owner && rmp->mp_lwp_group != owner)
		continue;
	if (sigismember(&rmp->mp_sigpending, signo))
		return TRUE;
  }
  return FALSE;
}

/* The timer went off, and 'missed' periods more went by: raise its signal,
 * or count overruns.
 */
static void
fire(struct ptimer *pt, u64_t missed)
{
  struct mproc *rmp;
  struct pm_siginfo saved;

  if (pt->pt_notify != SIGEV_SIGNAL)
	return;

  /* Our last signal may have gone without being delivered to a handler:
   * ignored, taken by sigwait(2), or acted on by default.
   */
  if (pt->pt_queued && !pending_in_process(pt->pt_owner, pt->pt_signo))
	pt->pt_queued = FALSE;

  if (pt->pt_queued) {
	add_overruns(pt, 1 + missed);
	return;
  }

  rmp = &mproc[pt->pt_owner];
  pt->pt_queued = TRUE;
  pt->pt_overrun = 0;
  add_overruns(pt, missed);
  saved = sig_origin;
  set_sig_origin(SI_TIMER, 0, 0, pt->pt_id, 0);
  sig_origin.ps_value = pt->pt_value;
  mp = &mproc[0];			/* the signal comes from PM */
  check_sig(rmp->mp_pid, pt->pt_signo, FALSE /* ksig */);
  sig_origin = saved;
}

/* Watchdog callback: 'arg' is the timer's index in ptimers. */
static void
ptimer_expire(int arg)
{
  struct ptimer *pt;
  struct mproc *rmp;
  u64_t now;

  if (arg < 0 || arg >= NR_PTIMERS) return;
  pt = &ptimers[arg];
  if (!pt->pt_inuse || pt->pt_expires == 0) return;
  rmp = &mproc[pt->pt_owner];
  if ((rmp->mp_flags & (IN_USE | EXITING)) != IN_USE ||
      rmp->mp_endpoint != pt->pt_endpt)
	return;

  /* We are at or past the tick of the expiry, which is at or past the
   * expiry itself, unless the timer was too far off to set in one go.
   */
  now = uptime_ns(NULL);
  if (now < pt->pt_expires) {
	arm(pt);
	return;
  }

  /* On to the next period before raising the signal, which may be taken at
   * once (by sigwait(2)), and its delivery count the periods up to then.
   * Those that went by entirely already are overruns.
   */
  if (pt->pt_interval == 0) {
	pt->pt_expires = 0;
	fire(pt, 0);
	return;
  }
  pt->pt_expires += pt->pt_interval;
  fire(pt, skip_periods(pt, now));

  /* The signal may have ended the process, and deleted its timers. */
  if (pt->pt_inuse)
	arm(pt);
}

/*===========================================================================*
 *				ptimer_delivered			     *
 *===========================================================================*/
void
ptimer_delivered(int slot, int id)
{
/* The SI_TIMER signal of timer 'id' is going to a handler in the process
 * that 'slot' belongs to: its overrun count becomes what timer_getoverrun(2)
 * reports, and later expiries raise the signal again.
 */
  struct ptimer *pt;

  if ((pt = find_timer(owner_slot(&mproc[slot]), id)) == NULL)
	return;
  /* Up to now, not just to the last tick. */
  if (pt->pt_queued && pt->pt_expires != 0)
	add_overruns(pt, skip_periods(pt, uptime_ns(NULL)));
  pt->pt_overrun_last = pt->pt_overrun;
  pt->pt_overrun = 0;
  pt->pt_queued = FALSE;
}

/*===========================================================================*
 *				ptimer_release				     *
 *===========================================================================*/
void
ptimer_release(struct mproc *rmp)
{
/* Delete the timers of the process 'rmp' leads (exec, exit).  A thread
 * owns none, so this does nothing for it.
 */
  struct ptimer *pt;
  int owner;

  owner = (int) (rmp - mproc);
  for (pt = &ptimers[0]; pt < &ptimers[NR_PTIMERS]; pt++) {
	if (pt->pt_inuse && pt->pt_owner == owner) {
		disarm(pt);
		pt->pt_inuse = FALSE;
	}
  }
}

static int
timer_create_op(int owner)
{
  struct ptimer *pt, *free_pt = NULL;
  int clock, notify, signo, id, count = 0;
  unsigned int used = 0;

  clock = m_in.m_lc_pm_timer.clock;
  notify = m_in.m_lc_pm_timer.notify;
  signo = m_in.m_lc_pm_timer.signo;

  if (clock != CLOCK_REALTIME && clock != CLOCK_MONOTONIC)
	return EINVAL;
  if (notify != SIGEV_NONE && notify != SIGEV_SIGNAL)
	return EINVAL;			/* SIGEV_THREAD: as on NetBSD */
  if (notify == SIGEV_SIGNAL && (signo <= 0 || signo >= _NSIG))
	return EINVAL;

  for (pt = &ptimers[0]; pt < &ptimers[NR_PTIMERS]; pt++) {
	if (!pt->pt_inuse) {
		if (free_pt == NULL) free_pt = pt;
	} else if (pt->pt_owner == owner) {
		count++;
		used |= 1U << pt->pt_id;
	}
  }
  if (count >= PTIMER_MAX || free_pt == NULL)
	return EAGAIN;
  for (id = 0; used & (1U << id); id++)
	;

  pt = free_pt;
  memset(pt, 0, sizeof(*pt));
  pt->pt_inuse = TRUE;
  pt->pt_owner = owner;
  pt->pt_endpt = mproc[owner].mp_endpoint;
  pt->pt_id = id;
  pt->pt_clock = clock;
  pt->pt_notify = notify;
  pt->pt_signo = signo;
  pt->pt_value = (m_in.m_lc_pm_timer.flags & PM_TIMER_VALUE_ID) ?
	(vir_bytes) id : m_in.m_lc_pm_timer.value;
  init_timer(&pt->pt_tmr);
  return id;
}

/* Report the time left and the interval of 'pt' to the caller. */
static int
copy_out(struct ptimer *pt, vir_bytes addr)
{
  struct itimerspec its;
  u64_t now;

  memset(&its, 0, sizeof(its));
  if (pt->pt_expires != 0) {
	now = uptime_ns(NULL);
	/* Due, but not gone off yet: a little time is left still. */
	timespec_from_ns(&its.it_value, pt->pt_expires > now ?
		pt->pt_expires - now : 1);
  }
  timespec_from_ns(&its.it_interval, pt->pt_interval);
  return sys_datacopy(SELF, (vir_bytes) &its, who_e, addr, sizeof(its));
}

static int
timer_settime_op(struct ptimer *pt)
{
  struct itimerspec its;
  clock_t uptime, realtime;
  time_t boottime;
  u64_t now, value, interval;
  int64_t rt_ahead;
  int r, precise;

  if ((r = sys_datacopy(who_e, m_in.m_lc_pm_timer.itp, SELF,
      (vir_bytes) &its, sizeof(its))) != OK)
	return r;
  if ((r = ns_from_timespec(&its.it_value, &value)) != OK ||
      (r = ns_from_timespec(&its.it_interval, &interval)) != OK)
	return r;

  if (m_in.m_lc_pm_timer.oitp != 0 &&
      (r = copy_out(pt, m_in.m_lc_pm_timer.oitp)) != OK)
	return r;

  disarm(pt);
  if (value == 0)
	return OK;			/* disarm only */

  now = uptime_ns(&precise);
  if (m_in.m_lc_pm_timer.flags & TIMER_ABSTIME) {
	/* An absolute time on the timer's clock, which for CLOCK_REALTIME
	 * is ahead of the monotonic one by the boot time and the ticks it has
	 * gained since (see do_gettime).  A time that has passed expires at
	 * the next tick.
	 */
	if (pt->pt_clock == CLOCK_REALTIME) {
		if ((r = getuptime(&uptime, &realtime, &boottime)) != OK)
			return r;
		rt_ahead = (int64_t) boottime * (int64_t) NSEC +
		    (int64_t) (int32_t) (realtime - uptime) *
		    (int64_t) NSEC / (int64_t) system_hz;
		value = ((int64_t) value > rt_ahead) ?
		    (u64_t) ((int64_t) value - rt_ahead) : 0;
	}
	pt->pt_expires = (value > now) ? value : now;
  } else {
	/* From now; without the TSC we know it only to the last tick, and
	 * now may be up to a tick later.
	 */
	if (!precise)
		now += NSEC / system_hz;
	pt->pt_expires = now + value;
  }
  if (pt->pt_expires == 0) pt->pt_expires = 1;
  pt->pt_interval = interval;
  arm(pt);
  return OK;
}

/*===========================================================================*
 *				do_timer				     *
 *===========================================================================*/
int
do_timer(void)
{
/* Perform the PM_TIMER system call: timer_create(2) and co. */
  struct ptimer *pt;
  int owner, r;

  owner = owner_slot(mp);

  if (m_in.m_lc_pm_timer.op == PM_TIMER_CREATE)
	return timer_create_op(owner);

  if ((pt = find_timer(owner, m_in.m_lc_pm_timer.id)) == NULL)
	return EINVAL;

  switch (m_in.m_lc_pm_timer.op) {
  case PM_TIMER_DELETE:
	disarm(pt);
	pt->pt_inuse = FALSE;
	return OK;
  case PM_TIMER_SETTIME:
	return timer_settime_op(pt);
  case PM_TIMER_GETTIME:
	return copy_out(pt, m_in.m_lc_pm_timer.oitp);
  case PM_TIMER_GETOVERRUN:
	return pt->pt_overrun_last;
  default:
	r = EINVAL;
  }
  return r;
}
