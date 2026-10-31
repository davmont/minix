#include <sys/cdefs.h>
#include <lib.h>
#include "namespace.h"

#include <errno.h>
#include <string.h>
#include <signal.h>

int __sigreturn(void);
int __libc_sigaction_cancel(const struct sigaction *, struct sigaction *);

static int
do_sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_pm_sig.nr = sig;
  m.m_lc_pm_sig.act = (vir_bytes)act;
  m.m_lc_pm_sig.oact = (vir_bytes)oact;
  m.m_lc_pm_sig.ret = (vir_bytes)__sigreturn;

  return(_syscall(PM_PROC_NR, PM_SIGACTION, &m));
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
  struct sigaction sa;

  /* The cancellation signal is libpthread's, and no handler may hold it
   * off. */
  if (sig == __SIGCANCEL) {
	errno = EINVAL;
	return(-1);
  }
  if (act != NULL && sigismember(&act->sa_mask, __SIGCANCEL)) {
	sa = *act;
	sigdelset(&sa.sa_mask, __SIGCANCEL);
	act = &sa;
  }
  return(do_sigaction(sig, act, oact));
}

/* For libpthread: the handler of the cancellation signal. */
int
__libc_sigaction_cancel(const struct sigaction *act, struct sigaction *oact)
{

  return(do_sigaction(__SIGCANCEL, act, oact));
}

#if defined(__minix) && defined(__weak_alias)
__weak_alias(sigaction, __sigaction14)
#endif
