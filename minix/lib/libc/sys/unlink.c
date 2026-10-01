#include <sys/cdefs.h>
#include <lib.h>
#include "namespace.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

static int __unlinkat(int dirfd, const char *name, int flags)
{
  message m;

  memset(&m, 0, sizeof(m));
  _loadnameat(dirfd, name, &m);
  m.m_lc_vfs_pathat.flags = flags;
  return(_syscall(VFS_PROC_NR, VFS_UNLINKAT, &m));
}

int unlinkat(int dirfd, const char *name, int flags)
{
  return __unlinkat(dirfd, name, flags);
}

int unlink(const char *name)
{
  return __unlinkat(AT_FDCWD, name, 0);
}
