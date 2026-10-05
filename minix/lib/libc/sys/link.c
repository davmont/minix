#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

static int __linkat(int fd1, const char *name, int fd2, const char *name2,
	int flags)
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_vfs_linkat.len1 = 0;	/* VFS copies the paths */
  m.m_lc_vfs_linkat.len2 = 0;
  m.m_lc_vfs_linkat.name1 = (vir_bytes)name;
  m.m_lc_vfs_linkat.name2 = (vir_bytes)name2;
  m.m_lc_vfs_linkat.fd1 = fd1;
  m.m_lc_vfs_linkat.fd2 = fd2;
  m.m_lc_vfs_linkat.flags = flags;
  return(_syscall(VFS_PROC_NR, VFS_LINKAT, &m));
}

int linkat(int fd1, const char *name, int fd2, const char *name2, int flags)
{
  return __linkat(fd1, name, fd2, name2, flags);
}

int link(const char *name, const char *name2)
{
  /* link(2) follows a symbolic link 'name', as it always has here (POSIX
   * leaves it open; linkat(2) without AT_SYMLINK_FOLLOW does not). */
  return __linkat(AT_FDCWD, name, AT_FDCWD, name2, AT_SYMLINK_FOLLOW);
}
