#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

static int __faccessat(int dirfd, const char *name, int mode, int flags)
{
  message m;

  memset(&m, 0, sizeof(m));
  _loadnameat(dirfd, name, &m);
  m.m_lc_vfs_pathat.mode = mode;
  m.m_lc_vfs_pathat.flags = flags;
  return(_syscall(VFS_PROC_NR, VFS_FACCESSAT, &m));
}

int faccessat(int dirfd, const char *name, int mode, int flags)
{
  return __faccessat(dirfd, name, mode, flags);
}

int access(const char *name, int mode)
{
  return __faccessat(AT_FDCWD, name, mode, 0);
}
