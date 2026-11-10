#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <string.h>
#include <unistd.h>

int _sys_fsync(int);

int _sys_fsync(int fd)
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_vfs_fsync.fd = fd;

  return(_syscall(VFS_PROC_NR, VFS_FSYNC, &m));
}

#ifdef __weak_alias
/* libpthread's cancellation points (pthread_cancelstub.c) override these. */
__weak_alias(fsync, _sys_fsync)
__weak_alias(_fsync, _sys_fsync)
#endif
