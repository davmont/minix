#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <fcntl.h>
#include <stdarg.h>
#include <string.h>

static int __openat(int dirfd, const char *name, int flags, va_list argp)
{
  message m;

  memset(&m, 0, sizeof(m));
  _loadnameat(dirfd, name, &m);
  m.m_lc_vfs_pathat.flags = flags;
  if (flags & O_CREAT)
	m.m_lc_vfs_pathat.mode = (mode_t)va_arg(argp, int);
  return (_syscall(VFS_PROC_NR, VFS_OPENAT, &m));
}

int openat(int dirfd, const char *name, int flags, ...)
{
  va_list argp;
  int r;

  va_start(argp, flags);
  r = __openat(dirfd, name, flags, argp);
  va_end(argp);
  return r;
}

int open(const char *name, int flags, ...)
{
  va_list argp;
  int r;

  va_start(argp, flags);
  r = __openat(AT_FDCWD, name, flags, argp);
  va_end(argp);
  return r;
}
