#include <sys/cdefs.h>
#include <lib.h>
#include "namespace.h"

#include <string.h>
#include <unistd.h>

ssize_t _sys_write(int, const void *, size_t);

ssize_t _sys_write(int fd, const void *buffer, size_t nbytes)
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_vfs_readwrite.fd = fd;
  m.m_lc_vfs_readwrite.len = nbytes;
  m.m_lc_vfs_readwrite.buf = (vir_bytes)buffer;
  m.m_lc_vfs_readwrite.cum_io = 0;	/* reserved for future use */
  return(_syscall(VFS_PROC_NR, VFS_WRITE, &m));
}

#ifdef __weak_alias
/* libpthread's cancellation points (pthread_cancelstub.c) override these. */
__weak_alias(write, _sys_write)
__weak_alias(_write, _sys_write)
#endif
