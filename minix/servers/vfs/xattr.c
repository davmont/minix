/* This file handles the extended-attribute system calls: the BSD
 * extattr_{get,set,list,delete}_{fd,file,link} family.  VFS resolves the target
 * (path or open fd), enforces the namespace's access policy, and forwards the
 * operation to the owning file server; the attribute name is copied into a
 * local buffer and granted directly, while the (potentially large) value buffer
 * is granted straight into the calling process.
 *
 * The entry points into this file are:
 *   do_extattr:    perform every extattr_* system call
 */

#include "fs.h"
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <sys/extattr.h>
#include <minix/callnr.h>
#include "file.h"
#include "path.h"
#include <minix/vfsif.h>
#include "vnode.h"
#include "vmnt.h"

#define XA_GET		0
#define XA_SET		1
#define XA_LIST		2
#define XA_DELETE	3

/*===========================================================================*
 *				xattr_value_rpc				     *
 *===========================================================================*/
static int xattr_value_rpc(endpoint_t fs_e, message *m, cp_grant_id_t *gfield,
	endpoint_t proc_e, vir_bytes buf, size_t len, int wrflag)
{
/* Send a request whose value buffer lives in the calling process.  The buffer
 * is granted as a magic grant into 'proc_e'; if the user page first needs to be
 * faulted in (GRANT_FAULTED), we ask VM to do so and retry once -- the same
 * dance as req_rdlink()/req_breadwrite().  'wrflag' is nonzero when the file
 * server writes into the buffer (get/list).  For a zero-length buffer (a size
 * query) no grant is made. */
  cp_grant_id_t g;
  int r, perm;

  if (len == 0) {
	*gfield = GRANT_INVALID;
	return fs_sendrec(fs_e, m);
  }

  perm = wrflag ? CPF_WRITE : CPF_READ;

  g = cpf_grant_magic(fs_e, proc_e, buf, len, perm | CPF_TRY);
  if (g == GRANT_INVALID) return(EINVAL);
  *gfield = g;
  r = fs_sendrec(fs_e, m);

  if (cpf_revoke(g) == GRANT_FAULTED) {
	if ((r = vm_vfs_procctl_handlemem(proc_e, buf, len, wrflag)) != OK)
		return(r);
	g = cpf_grant_magic(fs_e, proc_e, buf, len, perm);
	if (g == GRANT_INVALID) return(EINVAL);
	*gfield = g;
	r = fs_sendrec(fs_e, m);
	cpf_revoke(g);
  }

  return(r);
}

/*===========================================================================*
 *				req_getxattr				     *
 *===========================================================================*/
static int req_getxattr(endpoint_t fs_e, ino_t inode_nr, int ns,
	const char *name, size_t namelen, endpoint_t proc_e, vir_bytes buf,
	size_t len)
{
  message m;
  cp_grant_id_t gname;
  int r;

  memset(&m, 0, sizeof(m));
  m.m_type = REQ_GETXATTR;
  m.m_vfs_fs_getxattr.inode = inode_nr;
  m.m_vfs_fs_getxattr.namespace = ns;
  m.m_vfs_fs_getxattr.name_len = namelen;
  m.m_vfs_fs_getxattr.value_len = len;

  gname = cpf_grant_direct(fs_e, (vir_bytes) name, namelen, CPF_READ);
  if (gname == GRANT_INVALID) panic("req_getxattr: cpf_grant_direct failed");
  m.m_vfs_fs_getxattr.grant_name = gname;

  r = xattr_value_rpc(fs_e, &m, &m.m_vfs_fs_getxattr.grant_value, proc_e, buf,
	len, TRUE /*wrflag*/);

  cpf_revoke(gname);

  if (r == OK) r = m.m_fs_vfs_xattr.nbytes;
  return(r);
}

/*===========================================================================*
 *				req_listxattr				     *
 *===========================================================================*/
static int req_listxattr(endpoint_t fs_e, ino_t inode_nr, int ns,
	endpoint_t proc_e, vir_bytes buf, size_t len)
{
  message m;
  int r;

  memset(&m, 0, sizeof(m));
  m.m_type = REQ_LISTXATTR;
  m.m_vfs_fs_listxattr.inode = inode_nr;
  m.m_vfs_fs_listxattr.namespace = ns;
  m.m_vfs_fs_listxattr.mem_size = len;

  r = xattr_value_rpc(fs_e, &m, &m.m_vfs_fs_listxattr.grant, proc_e, buf, len,
	TRUE /*wrflag*/);

  if (r == OK) r = m.m_fs_vfs_xattr.nbytes;
  return(r);
}

/*===========================================================================*
 *				req_setxattr				     *
 *===========================================================================*/
static int req_setxattr(endpoint_t fs_e, ino_t inode_nr, int ns,
	const char *name, size_t namelen, endpoint_t proc_e, vir_bytes buf,
	size_t len)
{
  message m;
  cp_grant_id_t gname;
  int r;

  memset(&m, 0, sizeof(m));
  m.m_type = REQ_SETXATTR;
  m.m_vfs_fs_setxattr.inode = inode_nr;
  m.m_vfs_fs_setxattr.namespace = ns;
  m.m_vfs_fs_setxattr.name_len = namelen;
  m.m_vfs_fs_setxattr.value_len = len;
  m.m_vfs_fs_setxattr.flags = 0;	/* BSD has no flags; reserved for Linux */

  gname = cpf_grant_direct(fs_e, (vir_bytes) name, namelen, CPF_READ);
  if (gname == GRANT_INVALID) panic("req_setxattr: cpf_grant_direct failed");
  m.m_vfs_fs_setxattr.grant_name = gname;

  r = xattr_value_rpc(fs_e, &m, &m.m_vfs_fs_setxattr.grant_value, proc_e, buf,
	len, FALSE /*wrflag*/);

  cpf_revoke(gname);
  return(r);
}

/*===========================================================================*
 *				req_removexattr				     *
 *===========================================================================*/
static int req_removexattr(endpoint_t fs_e, ino_t inode_nr, int ns,
	const char *name, size_t namelen)
{
  message m;
  cp_grant_id_t gname;
  int r;

  memset(&m, 0, sizeof(m));
  m.m_type = REQ_REMOVEXATTR;
  m.m_vfs_fs_removexattr.inode = inode_nr;
  m.m_vfs_fs_removexattr.namespace = ns;
  m.m_vfs_fs_removexattr.name_len = namelen;

  gname = cpf_grant_direct(fs_e, (vir_bytes) name, namelen, CPF_READ);
  if (gname == GRANT_INVALID) panic("req_removexattr: cpf_grant_direct failed");
  m.m_vfs_fs_removexattr.grant_name = gname;

  r = fs_sendrec(fs_e, &m);

  cpf_revoke(gname);
  return(r);
}

/*===========================================================================*
 *				do_extattr				     *
 *===========================================================================*/
int do_extattr(void)
{
/* Perform an extattr_{get,set,list,delete}_{fd,file,link}() system call. */
  struct filp *flp;
  struct vnode *vp;
  struct vmnt *vmp;
  struct lookup resolve;
  char fullpath[PATH_MAX];
  char name[NAME_MAX + 1];
  vir_bytes nameaddr, dataaddr, pathaddr;
  size_t namelen, datalen, pathlen;
  int r, op, isfd, follow, ns, vlock, writing, rfd;
  endpoint_t proc_e = who_e;

  flp = NULL;
  vp = NULL;
  vmp = NULL;

  /* Classify the call: operation, fd vs path. */
  switch (job_call_nr) {
  case VFS_EXTATTR_GET:		op = XA_GET;    isfd = 0; break;
  case VFS_EXTATTR_SET:		op = XA_SET;    isfd = 0; break;
  case VFS_EXTATTR_LIST:	op = XA_LIST;   isfd = 0; break;
  case VFS_EXTATTR_DELETE:	op = XA_DELETE; isfd = 0; break;
  case VFS_EXTATTR_GET_FD:	op = XA_GET;    isfd = 1; break;
  case VFS_EXTATTR_SET_FD:	op = XA_SET;    isfd = 1; break;
  case VFS_EXTATTR_LIST_FD:	op = XA_LIST;   isfd = 1; break;
  case VFS_EXTATTR_DELETE_FD:	op = XA_DELETE; isfd = 1; break;
  default:			return(ENOSYS);
  }
  writing = (op == XA_SET || op == XA_DELETE);
  vlock = writing ? VNODE_WRITE : VNODE_READ;

  /* Extract message fields. */
  if (isfd) {
	rfd      = job_m_in.m_lc_vfs_extattr_fd.fd;
	nameaddr = job_m_in.m_lc_vfs_extattr_fd.name;
	namelen  = job_m_in.m_lc_vfs_extattr_fd.name_len;
	dataaddr = job_m_in.m_lc_vfs_extattr_fd.data;
	datalen  = job_m_in.m_lc_vfs_extattr_fd.data_len;
	ns       = job_m_in.m_lc_vfs_extattr_fd.namespace;
	follow   = TRUE;
	pathaddr = 0;
	pathlen  = 0;
  } else {
	rfd      = -1;
	pathaddr = job_m_in.m_lc_vfs_extattr.path;
	pathlen  = job_m_in.m_lc_vfs_extattr.path_len;
	nameaddr = job_m_in.m_lc_vfs_extattr.name;
	namelen  = job_m_in.m_lc_vfs_extattr.name_len;
	dataaddr = job_m_in.m_lc_vfs_extattr.data;
	datalen  = job_m_in.m_lc_vfs_extattr.data_len;
	ns       = job_m_in.m_lc_vfs_extattr.namespace;
	follow   = job_m_in.m_lc_vfs_extattr.follow;
  }

  /* Copy in the attribute name (every operation except list names one). */
  if (op != XA_LIST) {
	if (namelen == 0 || namelen > sizeof(name)) return(EINVAL);
	if ((r = sys_datacopy_wrapper(proc_e, nameaddr, VFS_PROC_NR,
	    (vir_bytes) name, namelen)) != OK)
		return(r);
	if (name[namelen - 1] != '\0') return(EINVAL);
  }

  /* Resolve the target vnode. */
  if (isfd) {
	if ((flp = get_filp(rfd, vlock)) == NULL) return(err_code);
	vp = flp->filp_vno;
	assert(vp != NULL);
	dup_vnode(vp);
  } else {
	lookup_init(&resolve, fullpath,
	    follow ? PATH_NOFLAGS : PATH_RET_SYMLINK, &vmp, &vp);
	resolve.l_vmnt_lock = VMNT_READ;
	resolve.l_vnode_lock = vlock;
	if (fetch_name(pathaddr, pathlen, fullpath) != OK) return(err_code);
	if ((vp = eat_path(&resolve, fp)) == NULL) return(err_code);
  }

  /* Enforce the namespace's access policy.  The system namespace is reserved
   * to the super-user -- except the POSIX ACL attributes, which live there but
   * follow file-ownership rules (like chmod): the owner (or super-user) may
   * change them, and anyone who can reach the file may read them, just as the
   * mode is public.  The user namespace follows the file's read/write bits. */
  if (ns == EXTATTR_NAMESPACE_SYSTEM) {
	int is_acl = (op != XA_LIST && (!strcmp(name, "posix_acl_access") ||
	    !strcmp(name, "posix_acl_default")));
	if (is_acl && !writing)
		r = OK;
	else if (is_acl)
		r = (vp->v_uid == fp->fp_effuid || fp->fp_effuid == SU_UID) ?
		    OK : EPERM;
	else
		r = (fp->fp_effuid == SU_UID) ? OK : EPERM;
  } else if (ns == EXTATTR_NAMESPACE_USER)
	r = forbidden(fp, vp, writing ? W_BIT : R_BIT);
  else
	r = EINVAL;

  if (r == OK && writing)
	r = read_only(vp);

  if (r == OK) {
	switch (op) {
	case XA_GET:
		r = req_getxattr(vp->v_fs_e, vp->v_inode_nr, ns, name, namelen,
		    proc_e, dataaddr, datalen);
		break;
	case XA_LIST:
		r = req_listxattr(vp->v_fs_e, vp->v_inode_nr, ns, proc_e,
		    dataaddr, datalen);
		break;
	case XA_SET:
		r = req_setxattr(vp->v_fs_e, vp->v_inode_nr, ns, name, namelen,
		    proc_e, dataaddr, datalen);
		break;
	case XA_DELETE:
		r = req_removexattr(vp->v_fs_e, vp->v_inode_nr, ns, name,
		    namelen);
		break;
	}
  }

  if (isfd) {
	unlock_filp(flp);
  } else {
	unlock_vnode(vp);
	unlock_vmnt(vmp);
  }
  put_vnode(vp);
  return(r);
}
