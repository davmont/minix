/* sigqueue(2), sigtimedwait(2), sigwaitinfo(2) and sigwait(3): PM_SIGQUEUE
 * and PM_SIGTIMEDWAIT.
 */
#include <sys/cdefs.h>
#include <lib.h>

#include <errno.h>
#include <string.h>
#include <signal.h>
#include <time.h>

int
sigqueue(pid_t pid, int signo, const union sigval value)
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_pm_sigqueue.pid = pid;
  m.m_lc_pm_sigqueue.sig = signo;
  m.m_lc_pm_sigqueue.value = (vir_bytes) value.sival_ptr;
  return(_syscall(PM_PROC_NR, PM_SIGQUEUE, &m));
}

/* <signal.h> names this one __sigtimedwait50, as on NetBSD. */
int
sigtimedwait(const sigset_t * __restrict set, siginfo_t * __restrict info,
	const struct timespec * __restrict timeout)
{
  message m;

  if (set == NULL) {
	errno = EFAULT;
	return(-1);
  }
  memset(&m, 0, sizeof(m));
  m.m_lc_pm_sigtimedwait.set = *set;
  m.m_lc_pm_sigtimedwait.info = (vir_bytes) info;
  m.m_lc_pm_sigtimedwait.timeout = (vir_bytes) timeout;
  return(_syscall(PM_PROC_NR, PM_SIGTIMEDWAIT, &m));
}

int
sigwaitinfo(const sigset_t * __restrict set, siginfo_t * __restrict info)
{

  return(sigtimedwait(set, info, NULL));
}

int
sigwait(const sigset_t * __restrict set, int * __restrict sig)
{
  int saved_errno = errno, r;

  /* POSIX: the error number is returned, errno is left alone, and a handler
   * that runs meanwhile does not end the wait. */
  while ((r = sigtimedwait(set, NULL, NULL)) < 0 && errno == EINTR)
	;
  if (r < 0) {
	r = errno;
	errno = saved_errno;
	return(r);
  }
  errno = saved_errno;
  *sig = r;
  return(0);
}
