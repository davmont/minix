#include <sys/cdefs.h>
#include <lib.h>
#include "namespace.h"

#include <string.h>

void _loadname(const char *name, message *msgptr)
{
/* This function is used to load a string into a type m3 message. If the
 * string fits in the message, it is copied there.  If not, a pointer to
 * it is passed.
 */
  register size_t k;

  k = strlen(name) + 1;
  msgptr->m_lc_vfs_path.len = k;
  msgptr->m_lc_vfs_path.name = (vir_bytes)name;
  if (k <= M_PATH_STRING_MAX) strcpy(msgptr->m_lc_vfs_path.buf, name);
}

void _loadnameat(int dirfd, const char *name, message *msgptr)
{
/* Load the start directory and path of a mess_lc_vfs_pathat message.  The
 * path is copied into the message if it fits, and passed by pointer always.
 */
  size_t k;

  k = strlen(name) + 1;
  msgptr->m_lc_vfs_pathat.dirfd = dirfd;
  msgptr->m_lc_vfs_pathat.len = k;
  msgptr->m_lc_vfs_pathat.name = (vir_bytes)name;
  if (k <= M_PATHAT_STRING_MAX) memcpy(msgptr->m_lc_vfs_pathat.buf, name, k);
}
