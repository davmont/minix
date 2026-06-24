#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <string.h>
#include <sys/stat.h>

/* The 32-bit file flags are carried in the message's mode field. */
int chflags(const char *name, unsigned long flags)
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_vfs_path.mode = (mode_t) flags;
  _loadname(name, &m);
  return(_syscall(VFS_PROC_NR, VFS_CHFLAGS, &m));
}
