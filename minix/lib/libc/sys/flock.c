#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

/*
 * flock(2): lock or unlock the whole file open as 'fd', on behalf of the open
 * file (shared by dup(2) and fork(2)), not of the process as fcntl(2) locks.
 */
int flock(int fd, int op)
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_vfs_fcntl.fd = fd;
  m.m_lc_vfs_fcntl.cmd = op;

  return _syscall(VFS_PROC_NR, VFS_FLOCK, &m);
}
