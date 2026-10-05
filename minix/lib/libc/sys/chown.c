#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#ifdef __weak_alias
__weak_alias(__posix_chown, chown)
__weak_alias(__posix_lchown, lchown)
#endif

static int __fchownat(int dirfd, const char *name, uid_t owner, gid_t grp,
	int flags)
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_vfs_chown.fd = dirfd;
  m.m_lc_vfs_chown.len = 0;	/* VFS copies the path */
  m.m_lc_vfs_chown.owner = owner;
  m.m_lc_vfs_chown.group = grp;
  m.m_lc_vfs_chown.name = (vir_bytes)name;
  m.m_lc_vfs_chown.flags = flags;
  return(_syscall(VFS_PROC_NR, VFS_FCHOWNAT, &m));
}

int fchownat(int dirfd, const char *name, uid_t owner, gid_t grp, int flags)
{
  return __fchownat(dirfd, name, owner, grp, flags);
}

int chown(const char *name, uid_t owner, gid_t grp)
{
  return __fchownat(AT_FDCWD, name, owner, grp, 0);
}

int lchown(const char *name, uid_t owner, gid_t grp)
{
  return __fchownat(AT_FDCWD, name, owner, grp, AT_SYMLINK_NOFOLLOW);
}
