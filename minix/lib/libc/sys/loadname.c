#include <sys/cdefs.h>
#include <lib.h>
#include "namespace.h"

#include <string.h>

/*
 * Paths are passed by address only, with length 0 ("not known"): VFS copies
 * the string itself, so that a bad address fails with EFAULT instead of
 * crashing the caller here, as on systems where the kernel copies paths.
 */
void _loadname(const char *name, message *msgptr)
{
  msgptr->m_lc_vfs_path.len = 0;
  msgptr->m_lc_vfs_path.name = (vir_bytes)name;
}

void _loadnameat(int dirfd, const char *name, message *msgptr)
{
  msgptr->m_lc_vfs_pathat.dirfd = dirfd;
  msgptr->m_lc_vfs_pathat.len = 0;
  msgptr->m_lc_vfs_pathat.name = (vir_bytes)name;
}
