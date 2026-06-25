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
#include <sys/acl.h>
#include <minix/callnr.h>
#include "file.h"
#include "path.h"
#include <minix/vfsif.h>
#include "vnode.h"
#include "vmnt.h"

/* A POSIX ACL with this many entries is far more than any normal file needs;
 * a larger one is treated as absent for access checks (the mode bits, kept in
 * sync with the ACL, then apply). */
#define VFS_ACL_MAXENT	128
#define VFS_ACL_BUFSZ	(sizeof(struct posix_acl_xattr_header) + \
			 VFS_ACL_MAXENT * sizeof(struct posix_acl_xattr_entry))

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
 *				vfs_getacl				     *
 *===========================================================================*/
static int
vfs_getacl(struct vnode *vp, char *buf, size_t size)
{
/* Fetch the access ACL of 'vp' into the VFS-local buffer 'buf'; return its
 * length, or a negative error (ENOATTR if the file has no access ACL). */
  static const char name[] = POSIX_ACL_XATTR_ACCESS;
  message m;
  cp_grant_id_t gname, gval;
  int r;

  gname = cpf_grant_direct(vp->v_fs_e, (vir_bytes) name, sizeof(name), CPF_READ);
  gval = cpf_grant_direct(vp->v_fs_e, (vir_bytes) buf, size, CPF_WRITE);
  if (gname == GRANT_INVALID || gval == GRANT_INVALID) {
	if (gname != GRANT_INVALID) cpf_revoke(gname);
	if (gval != GRANT_INVALID) cpf_revoke(gval);
	return(EGENERIC);
  }

  memset(&m, 0, sizeof(m));
  m.m_type = REQ_GETXATTR;
  m.m_vfs_fs_getxattr.inode = vp->v_inode_nr;
  m.m_vfs_fs_getxattr.namespace = EXTATTR_NAMESPACE_SYSTEM;
  m.m_vfs_fs_getxattr.grant_name = gname;
  m.m_vfs_fs_getxattr.name_len = sizeof(name);
  m.m_vfs_fs_getxattr.grant_value = gval;
  m.m_vfs_fs_getxattr.value_len = size;

  r = fs_sendrec(vp->v_fs_e, &m);

  cpf_revoke(gname);
  cpf_revoke(gval);

  if (r == OK) return((int) m.m_fs_vfs_xattr.nbytes);
  return(r);
}

/*===========================================================================*
 *				vfs_acl_eval				     *
 *===========================================================================*/
static int
vfs_acl_eval(const char *buf, size_t len, struct fproc *fp, uid_t fowner,
	gid_t fgroup, uid_t uid, gid_t gid, mode_t access)
{
/* Evaluate a POSIX.1e access ACL (POSIX_ACL_XATTR binary form) for the
 * requester, per the standard algorithm.  Returns OK or EACCES; EGENERIC if the
 * buffer is not a usable ACL (the caller then falls back to the mode bits). */
  const struct posix_acl_xattr_header *h = (const struct posix_acl_xattr_header *) buf;
  const struct posix_acl_xattr_entry *e;
  unsigned n, i;
  mode_t mask = ACL_PERM_BITS;		/* no MASK entry => no masking */
  int group_match = 0, granted = 0;

  if (len < sizeof(*h) || h->a_version != POSIX_ACL_XATTR_VERSION)
	return(EGENERIC);
  n = (len - sizeof(*h)) / sizeof(*e);
  e = (const struct posix_acl_xattr_entry *) (h + 1);
  access &= ACL_PERM_BITS;

  for (i = 0; i < n; i++)
	if (e[i].e_tag == ACL_MASK) mask = e[i].e_perm & ACL_PERM_BITS;

  /* Owner: the USER_OBJ entry, unmasked. */
  if (uid == fowner) {
	for (i = 0; i < n; i++)
		if (e[i].e_tag == ACL_USER_OBJ)
			return ((e[i].e_perm & access) == access) ? OK : EACCES;
	return(EACCES);
  }
  /* A named user matches exactly and is decisive. */
  for (i = 0; i < n; i++)
	if (e[i].e_tag == ACL_USER && (uid_t) e[i].e_id == uid)
		return (((e[i].e_perm & mask) & access) == access) ?
		    OK : EACCES;
  /* Group class: the owning group and any matching named groups.  Access is
   * granted if any matching entry (after the mask) allows it. */
  for (i = 0; i < n; i++) {
	if (e[i].e_tag == ACL_GROUP_OBJ &&
	    (gid == fgroup || in_group(fp, fgroup) == OK)) {
		group_match = 1;
		if (((e[i].e_perm & mask) & access) == access) granted = 1;
	} else if (e[i].e_tag == ACL_GROUP &&
	    (gid == (gid_t) e[i].e_id ||
	     in_group(fp, (gid_t) e[i].e_id) == OK)) {
		group_match = 1;
		if (((e[i].e_perm & mask) & access) == access) granted = 1;
	}
  }
  if (group_match) return granted ? OK : EACCES;
  /* Everyone else: the OTHER entry, unmasked. */
  for (i = 0; i < n; i++)
	if (e[i].e_tag == ACL_OTHER)
		return ((e[i].e_perm & access) == access) ? OK : EACCES;
  return(EACCES);
}

/*===========================================================================*
 *				vfs_acl_check				     *
 *===========================================================================*/
int
vfs_acl_check(struct vnode *vp, struct fproc *fp, uid_t uid, gid_t gid,
	mode_t access)
{
/* If 'vp' has an access ACL, evaluate it and return OK or EACCES.  If it has
 * none, return EGENERIC so the caller falls back to the file mode.  The has-ACL
 * state is cached on the vnode so the common no-ACL case stays cheap. */
  char buf[VFS_ACL_BUFSZ];
  int r;

  if (vp->v_acl == VACL_NONE)
	return(EGENERIC);

  r = vfs_getacl(vp, buf, sizeof(buf));
  if (r < (int) sizeof(struct posix_acl_xattr_header)) {
	/* ENOATTR, an error, or an oversized/garbage ACL: treat as absent. */
	vp->v_acl = VACL_NONE;
	return(EGENERIC);
  }
  vp->v_acl = VACL_PRESENT;
  return vfs_acl_eval(buf, (size_t) r, fp, vp->v_uid, vp->v_gid, uid, gid,
	access);
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

  /* A change to the access ACL invalidates the vnode's cached has-ACL state. */
  if ((op == XA_SET || op == XA_DELETE) && ns == EXTATTR_NAMESPACE_SYSTEM &&
      !strcmp(name, POSIX_ACL_XATTR_ACCESS))
	vp->v_acl = VACL_UNKNOWN;

  if (isfd) {
	unlock_filp(flp);
  } else {
	unlock_vnode(vp);
	unlock_vmnt(vmp);
  }
  put_vnode(vp);
  return(r);
}
