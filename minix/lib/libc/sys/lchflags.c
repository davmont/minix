#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <string.h>
#include <sys/stat.h>

/*
 * lchflags() sets the flags of a symbolic link itself rather than its target.
 * MINIX's VFS does not yet distinguish a no-follow path lookup for this call
 * (as is also the case for lchown), so this currently behaves like chflags();
 * for a non-symlink argument the two are identical.
 */
int lchflags(const char *name, unsigned long flags)
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_vfs_path.mode = (mode_t) flags;
  _loadname(name, &m);
  return(_syscall(VFS_PROC_NR, VFS_CHFLAGS, &m));
}
