#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>

#ifdef __weak_alias
__weak_alias(posix_fallocate, _posix_fallocate)
#endif

/*
 * posix_fallocate(2): allocate storage for bytes [offset, offset + len) of
 * the file open as 'fd'.  Like the other posix_* functions it returns the
 * error number instead of setting errno.
 */
int posix_fallocate(int fd, off_t offset, off_t len)
{
  message m;
  int r, saved_errno;

  memset(&m, 0, sizeof(m));
  m.m_lc_vfs_fallocate.fd = fd;
  m.m_lc_vfs_fallocate.offset = offset;
  m.m_lc_vfs_fallocate.len = len;

  saved_errno = errno;
  r = _syscall(VFS_PROC_NR, VFS_FALLOCATE, &m);
  if (r < 0) r = errno;
  errno = saved_errno;
  return r;
}
