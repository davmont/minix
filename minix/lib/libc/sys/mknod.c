#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

static int __mknodat(int dirfd, const char *name, mode_t mode, dev_t dev)
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_vfs_mknodat.dirfd = dirfd;
  m.m_lc_vfs_mknodat.len = strlen(name) + 1;
  m.m_lc_vfs_mknodat.mode = mode;
  m.m_lc_vfs_mknodat.device = dev;
  m.m_lc_vfs_mknodat.name = (vir_bytes)name;
  return(_syscall(VFS_PROC_NR, VFS_MKNODAT, &m));
}

int mknodat(int dirfd, const char *name, mode_t mode, dev_t dev)
{
  return __mknodat(dirfd, name, mode, dev);
}

int mknod(const char *name, mode_t mode, dev_t dev)
{
  return __mknodat(AT_FDCWD, name, mode, dev);
}

#if defined(__minix) && defined(__weak_alias)
__weak_alias(mknod, __mknod50)
#endif
