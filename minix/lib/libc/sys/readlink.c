#include <sys/cdefs.h>
#include <lib.h>
#include "namespace.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#ifdef __weak_alias
__weak_alias(readlink, _readlink)
#endif

static ssize_t __readlinkat(int dirfd, const char *name, char *buffer,
	size_t bufsiz)
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_vfs_readlinkat.dirfd = dirfd;
  m.m_lc_vfs_readlinkat.namelen = 0;	/* VFS copies the path */
  m.m_lc_vfs_readlinkat.bufsize = bufsiz;
  m.m_lc_vfs_readlinkat.name = (vir_bytes)name;
  m.m_lc_vfs_readlinkat.buf = (vir_bytes)buffer;

  return(_syscall(VFS_PROC_NR, VFS_READLINKAT, &m));
}

ssize_t readlinkat(int dirfd, const char *name, char *buffer, size_t bufsiz)
{
  return __readlinkat(dirfd, name, buffer, bufsiz);
}

ssize_t readlink(const char *name, char *buffer, size_t bufsiz)
{
  return __readlinkat(AT_FDCWD, name, buffer, bufsiz);
}
