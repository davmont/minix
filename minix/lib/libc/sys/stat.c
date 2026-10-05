#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#ifdef __weak_alias
__weak_alias(_stat, __stat50);
__weak_alias(_lstat, __lstat50);
__weak_alias(_fstat, __fstat50);

__weak_alias(stat, __stat50);
__weak_alias(lstat, __lstat50);
__weak_alias(fstat, __fstat50);
#endif

static int __fstatat(int dirfd, const char *name, struct stat *buffer,
	int flags)
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_vfs_fstatat.dirfd = dirfd;
  m.m_lc_vfs_fstatat.len = 0;	/* VFS copies the path */
  m.m_lc_vfs_fstatat.name = (vir_bytes)name;
  m.m_lc_vfs_fstatat.buf = (vir_bytes)buffer;
  m.m_lc_vfs_fstatat.flags = flags;

  return _syscall(VFS_PROC_NR, VFS_FSTATAT, &m);
}

int fstatat(int dirfd, const char *name, struct stat *buffer, int flags)
{
  return __fstatat(dirfd, name, buffer, flags);
}

int stat(const char *name, struct stat *buffer)
{
  return __fstatat(AT_FDCWD, name, buffer, 0);
}

int fstat(int fd, struct stat *buffer)
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_vfs_fstat.fd = fd;
  m.m_lc_vfs_fstat.buf = (vir_bytes)buffer;

  return _syscall(VFS_PROC_NR, VFS_FSTAT, &m);
}

int lstat(const char *name, struct stat *buffer)
{
  return __fstatat(AT_FDCWD, name, buffer, AT_SYMLINK_NOFOLLOW);
}
