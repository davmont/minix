/*
gettimeofday.c
*/

#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <string.h>
#include <sys/time.h>

int __minix_clock_fast(clockid_t, struct timespec *);

int gettimeofday(struct timeval *__restrict tp, void *__restrict tzp)
{
  struct timespec ts;
  message m;

  if (__minix_clock_fast(CLOCK_REALTIME, &ts) == 0) {
	tp->tv_sec = ts.tv_sec;
	tp->tv_usec = ts.tv_nsec / 1000;
	return 0;
  }

  memset(&m, 0, sizeof(m));

  if (_syscall(PM_PROC_NR, PM_GETTIMEOFDAY, &m) < 0)
  	return -1;

  tp->tv_sec = m.m_pm_lc_time.sec;
  tp->tv_usec = m.m_pm_lc_time.nsec / 1000;

  return 0;
}

