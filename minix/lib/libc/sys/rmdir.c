#include <sys/cdefs.h>
#include <lib.h>
#include "namespace.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

int rmdir(const char *name)
{
  message m;

  /* rmdir(2) is unlinkat(2) with AT_FDCWD and AT_REMOVEDIR. */
  memset(&m, 0, sizeof(m));
  _loadnameat(AT_FDCWD, name, &m);
  m.m_lc_vfs_pathat.flags = AT_REMOVEDIR;
  return(_syscall(VFS_PROC_NR, VFS_UNLINKAT, &m));
}
