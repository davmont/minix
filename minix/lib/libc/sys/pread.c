#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>

ssize_t _sys_pread(int, void *, size_t, off_t);

ssize_t _sys_pread(int fd, void *buffer, size_t nbytes, off_t where)
{
  message m;

  /* One call, so that it is atomic and leaves the file position alone. */
  memset(&m, 0, sizeof(m));
  m.m_lc_vfs_readwrite.fd = fd;
  m.m_lc_vfs_readwrite.buf = (vir_bytes)buffer;
  m.m_lc_vfs_readwrite.len = nbytes;
  m.m_lc_vfs_readwrite.offset = where;

  return _syscall(VFS_PROC_NR, VFS_PREAD, &m);
}

#ifdef __weak_alias
/* libpthread's cancellation points (pthread_cancelstub.c) override these. */
__weak_alias(pread, _sys_pread)
__weak_alias(_pread, _sys_pread)
#endif
