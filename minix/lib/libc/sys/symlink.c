#include <sys/cdefs.h>
#include <lib.h>
#include "namespace.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

/* Make 'name2' (starting at 'fd2' if relative) a symlink to 'name'. */
static int __symlinkat(const char *name, int fd2, const char *name2)
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_vfs_linkat.len1 = strlen(name) + 1;
  m.m_lc_vfs_linkat.len2 = strlen(name2) + 1;
  m.m_lc_vfs_linkat.name1 = (vir_bytes)name;
  m.m_lc_vfs_linkat.name2 = (vir_bytes)name2;
  m.m_lc_vfs_linkat.fd1 = AT_FDCWD;	/* unused: 'name' is not looked up */
  m.m_lc_vfs_linkat.fd2 = fd2;
  return(_syscall(VFS_PROC_NR, VFS_SYMLINKAT, &m));
}

int symlinkat(const char *name, int fd2, const char *name2)
{
  return __symlinkat(name, fd2, name2);
}

int symlink(const char *name, const char *name2)
{
  return __symlinkat(name, AT_FDCWD, name2);
}
