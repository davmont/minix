#include <sys/cdefs.h>
#include <lib.h>
#include "namespace.h"

#include <string.h>
#include <sys/time.h>
#include <sys/select.h>

int _sys___select50(int, fd_set *, fd_set *, fd_set *, struct timeval *);

int _sys___select50(int nfds,
	fd_set *readfds, fd_set *writefds, fd_set *errorfds,
	struct timeval *timeout)
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_vfs_select.nfds = nfds;
  m.m_lc_vfs_select.readfds = readfds;
  m.m_lc_vfs_select.writefds = writefds;
  m.m_lc_vfs_select.errorfds = errorfds;
  m.m_lc_vfs_select.timeout = (vir_bytes)timeout;

  return (_syscall(VFS_PROC_NR, VFS_SELECT, &m));
}

#ifdef __weak_alias
/* libpthread's cancellation points (pthread_cancelstub.c) override these. */
__weak_alias(__select50, _sys___select50)
__weak_alias(select, _sys___select50)
#endif
