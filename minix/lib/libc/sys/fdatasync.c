#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <string.h>
#include <unistd.h>

/*
 * MINIX implements file synchronisation at the granularity of a whole file
 * (data plus inode), so fdatasync() does the same work as fsync() -- which is
 * a correct, POSIX-conforming implementation: fsync() is a superset of
 * fdatasync(), and doing the extra (timestamp) metadata flush is always safe.
 */
int fdatasync(int fd)
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_vfs_fsync.fd = fd;

  return(_syscall(VFS_PROC_NR, VFS_FSYNC, &m));
}
