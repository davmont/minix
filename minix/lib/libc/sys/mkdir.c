#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

static int __mkdirat(int dirfd, const char *name, mode_t mode)
{
  message m;

  memset(&m, 0, sizeof(m));
  _loadnameat(dirfd, name, &m);
  m.m_lc_vfs_pathat.mode = mode;
  return(_syscall(VFS_PROC_NR, VFS_MKDIRAT, &m));
}

int mkdirat(int dirfd, const char *name, mode_t mode)
{
  return __mkdirat(dirfd, name, mode);
}

int mkdir(const char *name, mode_t mode)
{
  return __mkdirat(AT_FDCWD, name, mode);
}
