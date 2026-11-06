#include <sys/cdefs.h>
#include <lib.h>
#include "namespace.h"

#include <string.h>
#include <signal.h>

int _sys___sigsuspend14(const sigset_t *);

int _sys___sigsuspend14(set)
const sigset_t *set;
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_pm_sigset.set = *set;
  sigdelset(&m.m_lc_pm_sigset.set, __SIGCANCEL);	/* never blocked */
  return(_syscall(PM_PROC_NR, PM_SIGSUSPEND, &m));
}

#ifdef __weak_alias
/* libpthread's cancellation points (pthread_cancelstub.c) override these. */
__weak_alias(__sigsuspend14, _sys___sigsuspend14)
__weak_alias(sigsuspend, _sys___sigsuspend14)
#endif
