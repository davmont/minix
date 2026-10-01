#include <sys/cdefs.h>
#include <lib.h>
#include "namespace.h"

#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#ifdef __weak_alias
__weak_alias(__posix_rename, rename)
#endif

static int __renameat(int fd1, const char *name, int fd2, const char *name2)
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_vfs_linkat.len1 = strlen(name) + 1;
  m.m_lc_vfs_linkat.len2 = strlen(name2) + 1;
  m.m_lc_vfs_linkat.name1 = (vir_bytes)name;
  m.m_lc_vfs_linkat.name2 = (vir_bytes)name2;
  m.m_lc_vfs_linkat.fd1 = fd1;
  m.m_lc_vfs_linkat.fd2 = fd2;
  return(_syscall(VFS_PROC_NR, VFS_RENAMEAT, &m));
}

int renameat(int fd1, const char *name, int fd2, const char *name2)
{
  return __renameat(fd1, name, fd2, name2);
}

int rename(const char *name, const char *name2)
{
  return __renameat(AT_FDCWD, name, AT_FDCWD, name2);
}
