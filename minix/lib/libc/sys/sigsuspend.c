#include <sys/cdefs.h>
#include <lib.h>
#include "namespace.h"

#include <string.h>
#include <signal.h>

int sigsuspend(set)
const sigset_t *set;
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_pm_sigset.set = *set;
  sigdelset(&m.m_lc_pm_sigset.set, __SIGCANCEL);	/* never blocked */
  return(_syscall(PM_PROC_NR, PM_SIGSUSPEND, &m));
}

#if defined(__minix) && defined(__weak_alias)
__weak_alias(sigsuspend, __sigsuspend14)
#endif
