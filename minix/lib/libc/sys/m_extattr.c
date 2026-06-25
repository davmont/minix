/*
 * MINIX implementation of the BSD extended-attribute system calls:
 *   extattr_{get,set,list,delete}_{fd,file,link}()
 *
 * Each wrapper marshals its arguments into a VFS message and traps to VFS,
 * which resolves the target and forwards the operation to the owning file
 * server.  The attribute name and the value buffer are passed as user-space
 * pointers; VFS copies the name in and grants the value buffer directly.
 */
#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <string.h>
#include <sys/extattr.h>

static ssize_t
ext_path(int callnr, const char *path, int attrnamespace,
	const char *attrname, void *data, size_t nbytes, int follow)
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_vfs_extattr.path = (vir_bytes) __UNCONST(path);
  m.m_lc_vfs_extattr.path_len = (path != NULL) ? strlen(path) + 1 : 0;
  m.m_lc_vfs_extattr.name = (vir_bytes) __UNCONST(attrname);
  m.m_lc_vfs_extattr.name_len = (attrname != NULL) ? strlen(attrname) + 1 : 0;
  m.m_lc_vfs_extattr.data = (vir_bytes) data;
  m.m_lc_vfs_extattr.data_len = nbytes;
  m.m_lc_vfs_extattr.namespace = attrnamespace;
  m.m_lc_vfs_extattr.follow = follow;

  return (ssize_t) _syscall(VFS_PROC_NR, callnr, &m);
}

static ssize_t
ext_fd(int callnr, int fd, int attrnamespace, const char *attrname,
	void *data, size_t nbytes)
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_lc_vfs_extattr_fd.fd = fd;
  m.m_lc_vfs_extattr_fd.name = (vir_bytes) __UNCONST(attrname);
  m.m_lc_vfs_extattr_fd.name_len =
	(attrname != NULL) ? strlen(attrname) + 1 : 0;
  m.m_lc_vfs_extattr_fd.data = (vir_bytes) data;
  m.m_lc_vfs_extattr_fd.data_len = nbytes;
  m.m_lc_vfs_extattr_fd.namespace = attrnamespace;

  return (ssize_t) _syscall(VFS_PROC_NR, callnr, &m);
}

ssize_t
extattr_get_fd(int fd, int attrnamespace, const char *attrname, void *data,
	size_t nbytes)
{
  return ext_fd(VFS_EXTATTR_GET_FD, fd, attrnamespace, attrname, data, nbytes);
}

ssize_t
extattr_get_file(const char *path, int attrnamespace, const char *attrname,
	void *data, size_t nbytes)
{
  return ext_path(VFS_EXTATTR_GET, path, attrnamespace, attrname, data,
	nbytes, 1 /*follow*/);
}

ssize_t
extattr_get_link(const char *path, int attrnamespace, const char *attrname,
	void *data, size_t nbytes)
{
  return ext_path(VFS_EXTATTR_GET, path, attrnamespace, attrname, data,
	nbytes, 0 /*nofollow*/);
}

int
extattr_set_fd(int fd, int attrnamespace, const char *attrname,
	const void *data, size_t nbytes)
{
  return (int) ext_fd(VFS_EXTATTR_SET_FD, fd, attrnamespace, attrname,
	__UNCONST(data), nbytes);
}

int
extattr_set_file(const char *path, int attrnamespace, const char *attrname,
	const void *data, size_t nbytes)
{
  return (int) ext_path(VFS_EXTATTR_SET, path, attrnamespace, attrname,
	__UNCONST(data), nbytes, 1 /*follow*/);
}

int
extattr_set_link(const char *path, int attrnamespace, const char *attrname,
	const void *data, size_t nbytes)
{
  return (int) ext_path(VFS_EXTATTR_SET, path, attrnamespace, attrname,
	__UNCONST(data), nbytes, 0 /*nofollow*/);
}

ssize_t
extattr_list_fd(int fd, int attrnamespace, void *data, size_t nbytes)
{
  return ext_fd(VFS_EXTATTR_LIST_FD, fd, attrnamespace, NULL, data, nbytes);
}

ssize_t
extattr_list_file(const char *path, int attrnamespace, void *data,
	size_t nbytes)
{
  return ext_path(VFS_EXTATTR_LIST, path, attrnamespace, NULL, data, nbytes,
	1 /*follow*/);
}

ssize_t
extattr_list_link(const char *path, int attrnamespace, void *data,
	size_t nbytes)
{
  return ext_path(VFS_EXTATTR_LIST, path, attrnamespace, NULL, data, nbytes,
	0 /*nofollow*/);
}

int
extattr_delete_fd(int fd, int attrnamespace, const char *attrname)
{
  return (int) ext_fd(VFS_EXTATTR_DELETE_FD, fd, attrnamespace, attrname,
	NULL, 0);
}

int
extattr_delete_file(const char *path, int attrnamespace, const char *attrname)
{
  return (int) ext_path(VFS_EXTATTR_DELETE, path, attrnamespace, attrname,
	NULL, 0, 1 /*follow*/);
}

int
extattr_delete_link(const char *path, int attrnamespace, const char *attrname)
{
  return (int) ext_path(VFS_EXTATTR_DELETE, path, attrnamespace, attrname,
	NULL, 0, 0 /*nofollow*/);
}
