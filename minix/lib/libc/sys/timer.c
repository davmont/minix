#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <time.h>

/*
 * POSIX per-process timers.  PM keeps them (see minix/servers/pm/ptimer.c);
 * a timer_t is the timer's number within the process.
 */

static int
pm_timer(message *m, int op)
{

  m->m_lc_pm_timer.op = op;
  return _syscall(PM_PROC_NR, PM_TIMER, m);
}

int
timer_create(clockid_t clock_id, struct sigevent * __restrict evp,
	timer_t * __restrict timerid)
{
  message m;
  int r;

  if (timerid == NULL) {
	errno = EINVAL;
	return -1;
  }
  memset(&m, 0, sizeof(m));
  m.m_lc_pm_timer.clock = clock_id;
  if (evp == NULL) {
	/* POSIX: SIGALRM, with the timer's id as the value, which PM sets. */
	m.m_lc_pm_timer.notify = SIGEV_SIGNAL;
	m.m_lc_pm_timer.signo = SIGALRM;
	m.m_lc_pm_timer.flags = PM_TIMER_VALUE_ID;
  } else {
	m.m_lc_pm_timer.notify = evp->sigev_notify;
	m.m_lc_pm_timer.signo = evp->sigev_signo;
	m.m_lc_pm_timer.value = (vir_bytes) evp->sigev_value.sival_ptr;
  }
  if ((r = pm_timer(&m, PM_TIMER_CREATE)) < 0)
	return -1;
  *timerid = r;
  return 0;
}

int
timer_delete(timer_t timerid)
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_pm_timer.id = timerid;
  return pm_timer(&m, PM_TIMER_DELETE) < 0 ? -1 : 0;
}

int
__timer_settime50(timer_t timerid, int flags,
	const struct itimerspec * __restrict value,
	struct itimerspec * __restrict ovalue)
{
  message m;

  if (value == NULL) {
	errno = EINVAL;
	return -1;
  }
  memset(&m, 0, sizeof(m));
  m.m_lc_pm_timer.id = timerid;
  m.m_lc_pm_timer.flags = flags;
  m.m_lc_pm_timer.itp = (vir_bytes) value;
  m.m_lc_pm_timer.oitp = (vir_bytes) ovalue;
  return pm_timer(&m, PM_TIMER_SETTIME) < 0 ? -1 : 0;
}

int
__timer_gettime50(timer_t timerid, struct itimerspec *value)
{
  message m;

  if (value == NULL) {
	errno = EINVAL;
	return -1;
  }
  memset(&m, 0, sizeof(m));
  m.m_lc_pm_timer.id = timerid;
  m.m_lc_pm_timer.oitp = (vir_bytes) value;
  return pm_timer(&m, PM_TIMER_GETTIME) < 0 ? -1 : 0;
}

int
timer_getoverrun(timer_t timerid)
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_pm_timer.id = timerid;
  return pm_timer(&m, PM_TIMER_GETOVERRUN);
}
