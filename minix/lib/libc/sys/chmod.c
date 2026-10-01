#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>

static int __fchmodat(int dirfd, const char *name, mode_t mode, int flags)
{
  message m;

  memset(&m, 0, sizeof(m));
  _loadnameat(dirfd, name, &m);
  m.m_lc_vfs_pathat.mode = mode;
  m.m_lc_vfs_pathat.flags = flags;
  return(_syscall(VFS_PROC_NR, VFS_FCHMODAT, &m));
}

int fchmodat(int dirfd, const char *name, mode_t mode, int flags)
{
  return __fchmodat(dirfd, name, mode, flags);
}

int chmod(const char *name, mode_t mode)
{
  return __fchmodat(AT_FDCWD, name, mode, 0);
}

int lchmod(const char *name, mode_t mode)
{
  return __fchmodat(AT_FDCWD, name, mode, AT_SYMLINK_NOFOLLOW);
}
