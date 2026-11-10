#include <sys/cdefs.h>
#include <lib.h>
#include "namespace.h"

#include <string.h>
#include <unistd.h>

ssize_t _sys_read(int, void *, size_t);

ssize_t _sys_read(int fd, void *buffer, size_t nbytes)
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_vfs_readwrite.fd = fd;
  m.m_lc_vfs_readwrite.len = nbytes;
  m.m_lc_vfs_readwrite.buf = (vir_bytes)buffer;
  m.m_lc_vfs_readwrite.cum_io = 0;
  return(_syscall(VFS_PROC_NR, VFS_READ, &m));
}

#ifdef __weak_alias
/* libpthread's cancellation points (pthread_cancelstub.c) override these. */
__weak_alias(read, _sys_read)
__weak_alias(_read, _sys_read)
#endif
