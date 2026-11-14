#include <sys/cdefs.h>
#include <lib.h>
#include "namespace.h"

#include <string.h>
#include <sys/time.h>

#ifdef __weak_alias
__weak_alias(clock_getres, __clock_getres50);
#endif

int __minix_clock_fast_res(clockid_t, struct timespec *);

int clock_getres(clockid_t clock_id, struct timespec *res)
{
  message m;

  if (res != NULL && __minix_clock_fast_res(clock_id, res) == 0)
	return 0;

  memset(&m, 0, sizeof(m));
  m.m_lc_pm_time.clk_id = clock_id;

  if (_syscall(PM_PROC_NR, PM_CLOCK_GETRES, &m) < 0)
  	return -1;

  res->tv_sec = m.m_pm_lc_time.sec;
  res->tv_nsec = m.m_pm_lc_time.nsec;

  return 0;
}

