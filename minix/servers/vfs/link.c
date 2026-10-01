/* This file handles the LINK and UNLINK system calls.  It also deals with
 * deallocating the storage used by a file when the last UNLINK is done to a
 * file and the blocks must be returned to the free block pool.
 *
 * The entry points into this file are
 *   do_linkat:       perform the LINKAT system call
 *   do_unlinkat:     perform the UNLINKAT system call (unlink and rmdir)
 *   do_renameat:     perform the RENAMEAT system call
 *   do_truncate:     perform the TRUNCATE system call
 *   do_ftruncate:    perform the FTRUNCATE system call
 *   do_symlinkat:    perform the SYMLINKAT system call
 *   do_readlinkat:   perform the READLINKAT system call
 */

#include "fs.h"
#include <sys/stat.h>
#include <string.h>
#include <minix/com.h>
#include <minix/callnr.h>
#include <minix/vfsif.h>
#include <sys/dirent.h>
#include <assert.h>
#include "file.h"
#include "path.h"
#include "vnode.h"

static int unlink_path(char fullpath[PATH_MAX], struct vnode *start,
	int rmdir);
static int rename_path(char name1[PATH_MAX], struct vnode *start1,
	char name2[PATH_MAX], struct vnode *start2);

/*===========================================================================*
 *				do_linkat				     *
 *===========================================================================*/
int do_linkat(void)
{
/* Perform the linkat(fd1, name1, fd2, name2, flags) system call.  Like
 * POSIX, link a symbolic link 'name1' itself, unless AT_SYMLINK_FOLLOW.
 */
  int r = OK, flags;
  struct vnode *vp = NULL, *dirp = NULL, *start1 = NULL, *start2 = NULL;
  struct vmnt *vmp1 = NULL, *vmp2 = NULL;
  char fullpath[PATH_MAX];
  struct lookup resolve;
  vir_bytes vname1, vname2;
  size_t vname1_length, vname2_length;

  vname1 = job_m_in.m_lc_vfs_linkat.name1;
  vname1_length = job_m_in.m_lc_vfs_linkat.len1;
  vname2 = job_m_in.m_lc_vfs_linkat.name2;
  vname2_length = job_m_in.m_lc_vfs_linkat.len2;
  flags = job_m_in.m_lc_vfs_linkat.flags;

  if (flags & ~AT_SYMLINK_FOLLOW)
	return(EINVAL);

  lookup_init(&resolve, fullpath,
	(flags & AT_SYMLINK_FOLLOW) ? PATH_NOFLAGS : PATH_RET_SYMLINK,
	&vmp1, &vp);
  resolve.l_vmnt_lock = VMNT_WRITE;
  resolve.l_vnode_lock = VNODE_READ;

  /* See if 'name1' (file to be linked to) exists. */
  if (fetch_name(vname1, vname1_length, fullpath) != OK) return(err_code);
  if ((r = get_start_dir(job_m_in.m_lc_vfs_linkat.fd1, fullpath,
	&start1)) != OK)
	return(r);
  resolve.l_start = start1;
  if ((vp = eat_path(&resolve, fp)) == NULL) {
	put_start_dir(start1);
	return(err_code);
  }

  /* Does the final directory of 'name2' exist? */
  lookup_init(&resolve, fullpath, PATH_NOFLAGS, &vmp2, &dirp);
  resolve.l_vmnt_lock = VMNT_READ;
  resolve.l_vnode_lock = VNODE_WRITE;
  if (fetch_name(vname2, vname2_length, fullpath) != OK)
	r = err_code;
  else if ((r = get_start_dir(job_m_in.m_lc_vfs_linkat.fd2, fullpath,
	&start2)) == OK) {
	resolve.l_start = start2;
	if ((dirp = last_dir(&resolve, fp)) == NULL)
		r = err_code;
  }

  if (r != OK) {
	unlock_vnode(vp);
	unlock_vmnt(vmp1);
	put_vnode(vp);
	put_start_dir(start1);
	put_start_dir(start2);
	return(r);
  }

  /* Check for links across devices. */
  if (vp->v_fs_e != dirp->v_fs_e)
	r = EXDEV;
  else
	r = forbidden(fp, dirp, W_BIT | X_BIT);

  if (r == OK)
	r = req_link(vp->v_fs_e, dirp->v_inode_nr, fullpath,
		     vp->v_inode_nr);

  unlock_vnode(vp);
  unlock_vnode(dirp);
  if (vmp2 != NULL) unlock_vmnt(vmp2);
  unlock_vmnt(vmp1);
  put_vnode(vp);
  put_vnode(dirp);
  put_start_dir(start1);
  put_start_dir(start2);
  return(r);
}

/*===========================================================================*
 *				do_unlinkat				     *
 *===========================================================================*/
int do_unlinkat(void)
{
/* Perform the unlinkat(dirfd, name, flags) system call: rmdir(2) with
 * AT_REMOVEDIR, unlink(2) without.  The syscall might provide 'name' embedded
 * in the message.
 */
  struct vnode *start;
  int r, flags;
  char fullpath[PATH_MAX];

  flags = job_m_in.m_lc_vfs_pathat.flags;
  if (flags & ~AT_REMOVEDIR)
	return(EINVAL);

  if (copy_pathat(fullpath, sizeof(fullpath)) != OK)
	return(err_code);
  if ((r = get_start_dir(job_m_in.m_lc_vfs_pathat.dirfd, fullpath,
	&start)) != OK)
	return(r);

  r = unlink_path(fullpath, start, (flags & AT_REMOVEDIR));

  put_start_dir(start);
  return(r);
}

/*===========================================================================*
 *				unlink_path				     *
 *===========================================================================*/
static int unlink_path(char fullpath[PATH_MAX], struct vnode *start, int rmdir)
{
/* Remove the file, or with 'rmdir' the directory, named by 'fullpath'.  The
 * code for these two is almost the same.  They differ only in some condition
 * testing.  Unlink() may be used by the superuser to do dangerous things;
 * rmdir() may not.
 */
  struct vnode *dirp, *dirp_l, *vp;
  struct vmnt *vmp, *vmp2;
  int r;
  struct lookup resolve, stickycheck;

  lookup_init(&resolve, fullpath, PATH_RET_SYMLINK, &vmp, &dirp_l);
  resolve.l_start = start;
  resolve.l_vmnt_lock = VMNT_WRITE;
  resolve.l_vnode_lock = VNODE_WRITE;

  /* Get the last directory in the path. */
  if ((dirp = last_dir(&resolve, fp)) == NULL) return(err_code);

  /* Make sure that the object is a directory */
  if (!S_ISDIR(dirp->v_mode)) {
	unlock_vnode(dirp);
	unlock_vmnt(vmp);
	put_vnode(dirp);
	return(ENOTDIR);
  }

  /* The caller must have both search and execute permission */
  if ((r = forbidden(fp, dirp, X_BIT | W_BIT)) != OK) {
	unlock_vnode(dirp);
	unlock_vmnt(vmp);
	put_vnode(dirp);
	return(r);
  }

  /* Also, if the sticky bit is set, only the owner of the file or a privileged
     user is allowed to unlink */
  if ((dirp->v_mode & S_ISVTX) == S_ISVTX) {
	/* Look up inode of file to unlink to retrieve owner */
	lookup_init(&stickycheck, resolve.l_path, PATH_RET_SYMLINK, &vmp2, &vp);
	stickycheck.l_vmnt_lock = VMNT_READ;
	stickycheck.l_vnode_lock = VNODE_READ;
	vp = advance(dirp, &stickycheck, fp);
	assert(vmp2 == NULL);
	if (vp != NULL) {
		if (vp->v_uid != fp->fp_effuid && fp->fp_effuid != SU_UID)
			r = EPERM;
		unlock_vnode(vp);
		put_vnode(vp);
	} else
		r = err_code;
	if (r != OK) {
		unlock_vnode(dirp);
		unlock_vmnt(vmp);
		put_vnode(dirp);
		return(r);
	}
  }

  upgrade_vmnt_lock(vmp);

  if (!rmdir)
	  r = req_unlink(dirp->v_fs_e, dirp->v_inode_nr, fullpath);
  else
	  r = req_rmdir(dirp->v_fs_e, dirp->v_inode_nr, fullpath);
  unlock_vnode(dirp);
  unlock_vmnt(vmp);
  put_vnode(dirp);
  return(r);
}

/*===========================================================================*
 *				do_renameat				     *
 *===========================================================================*/
int do_renameat(void)
{
/* Perform the renameat(fd1, name1, fd2, name2) system call. */
  struct vnode *start1, *start2;
  char name1[PATH_MAX], name2[PATH_MAX];
  int r;

  if (fetch_name(job_m_in.m_lc_vfs_linkat.name1,
	job_m_in.m_lc_vfs_linkat.len1, name1) != OK)
	return(err_code);
  if (fetch_name(job_m_in.m_lc_vfs_linkat.name2,
	job_m_in.m_lc_vfs_linkat.len2, name2) != OK)
	return(err_code);

  if ((r = get_start_dir(job_m_in.m_lc_vfs_linkat.fd1, name1,
	&start1)) != OK)
	return(r);
  if ((r = get_start_dir(job_m_in.m_lc_vfs_linkat.fd2, name2,
	&start2)) != OK) {
	put_start_dir(start1);
	return(r);
  }

  r = rename_path(name1, start1, name2, start2);

  put_start_dir(start1);
  put_start_dir(start2);
  return(r);
}

/*===========================================================================*
 *				rename_path				     *
 *===========================================================================*/
static int rename_path(char name1[PATH_MAX], struct vnode *start1,
	char name2[PATH_MAX], struct vnode *start2)
{
/* Rename 'name1' to 'name2', each starting at its own directory if relative.
 * Both are overwritten by the lookups.
 */
  int r = OK, r1;
  struct vnode *old_dirp = NULL, *new_dirp = NULL, *new_dirp_l = NULL, *vp;
  struct vmnt *oldvmp, *newvmp, *vmp2;
  struct lookup resolve, stickycheck;

  lookup_init(&resolve, name1, PATH_RET_SYMLINK, &oldvmp, &old_dirp);
  resolve.l_start = start1;
  /* Do not yet request exclusive lock on vmnt to prevent deadlocks later on */
  resolve.l_vmnt_lock = VMNT_WRITE;
  resolve.l_vnode_lock = VNODE_WRITE;

  /* See if 'name1' (existing file) exists.  Get dir and file inodes. */
  if ((old_dirp = last_dir(&resolve, fp)) == NULL) return(err_code);

  /* If the sticky bit is set, only the owner of the file or a privileged
     user is allowed to rename */
  if ((old_dirp->v_mode & S_ISVTX) == S_ISVTX) {
	/* Look up inode of file to unlink to retrieve owner */
	lookup_init(&stickycheck, resolve.l_path, PATH_RET_SYMLINK, &vmp2, &vp);
	stickycheck.l_vmnt_lock = VMNT_READ;
	stickycheck.l_vnode_lock = VNODE_READ;
	vp = advance(old_dirp, &stickycheck, fp);
	assert(vmp2 == NULL);
	if (vp != NULL) {
		if(vp->v_uid != fp->fp_effuid && fp->fp_effuid != SU_UID)
			r = EPERM;
		unlock_vnode(vp);
		put_vnode(vp);
	} else
		r = err_code;
	if (r != OK) {
		unlock_vnode(old_dirp);
		unlock_vmnt(oldvmp);
		put_vnode(old_dirp);
		return(r);
	}
  }

  /* 'name1' is now the last component of the old name. */

  /* See if 'name2' (new name) exists.  Get dir inode */
  lookup_init(&resolve, name2, PATH_RET_SYMLINK, &newvmp, &new_dirp_l);
  resolve.l_start = start2;
  resolve.l_vmnt_lock = VMNT_READ;
  resolve.l_vnode_lock = VNODE_WRITE;
  if ((new_dirp = last_dir(&resolve, fp)) == NULL) r = err_code;

  /* We used a separate vnode pointer to see whether we obtained a lock on the
   * new_dirp vnode. If the new directory and old directory are the same, then
   * the VNODE_WRITE lock on new_dirp will fail. In that case, new_dirp_l will
   * be NULL, but new_dirp will not.
   */
  if (new_dirp == old_dirp) assert(new_dirp_l == NULL);

  if (r != OK) {
	unlock_vnode(old_dirp);
	unlock_vmnt(oldvmp);
	put_vnode(old_dirp);
	return(r);
  }

  /* Both parent directories must be on the same device. */
  if (old_dirp->v_fs_e != new_dirp->v_fs_e) r = EXDEV;

  /* Parent dirs must be writable, searchable and on a writable device */
  if ((r1 = forbidden(fp, old_dirp, W_BIT|X_BIT)) != OK ||
      (r1 = forbidden(fp, new_dirp, W_BIT|X_BIT)) != OK) r = r1;

  if (r == OK) {
	upgrade_vmnt_lock(oldvmp); /* Upgrade to exclusive access */
	r = req_rename(old_dirp->v_fs_e, old_dirp->v_inode_nr, name1,
		       new_dirp->v_inode_nr, name2);
  }

  unlock_vnode(old_dirp);
  unlock_vmnt(oldvmp);
  if (new_dirp_l) unlock_vnode(new_dirp_l);
  if (newvmp) unlock_vmnt(newvmp);

  put_vnode(old_dirp);
  put_vnode(new_dirp);

  return(r);
}

/*===========================================================================*
 *				do_truncate				     *
 *===========================================================================*/
int do_truncate(void)
{
/* truncate_vnode() does the actual work of do_truncate() and do_ftruncate().
 * do_truncate() and do_ftruncate() have to get hold of the inode, either
 * by name or fd, do checks on it, and call truncate_inode() to do the
 * work.
 */
  struct vnode *vp;
  struct vmnt *vmp;
  int r;
  char fullpath[PATH_MAX];
  struct lookup resolve;
  off_t length;
  vir_bytes vname;
  size_t vname_length;

  vname = job_m_in.m_lc_vfs_truncate.name;
  vname_length = job_m_in.m_lc_vfs_truncate.len;

  lookup_init(&resolve, fullpath, PATH_NOFLAGS, &vmp, &vp);
  resolve.l_vmnt_lock = VMNT_READ;
  resolve.l_vnode_lock = VNODE_WRITE;

  length = job_m_in.m_lc_vfs_truncate.offset;
  if (length < 0) return(EINVAL);

  /* Temporarily open file */
  if (fetch_name(vname, vname_length, fullpath) != OK) return(err_code);
  if ((vp = eat_path(&resolve, fp)) == NULL) return(err_code);

  /* Ask FS to truncate the file */
  if ((r = forbidden(fp, vp, W_BIT)) == OK) {
	/* If the file size does not change, do not make the actual call. This
	 * ensures that the file times are retained when the file size remains
	 * the same, which is a POSIX requirement.
	 */
	if (S_ISREG(vp->v_mode) && vp->v_size == length)
		r = OK;
	else
		r = truncate_vnode(vp, length);
  }

  unlock_vnode(vp);
  unlock_vmnt(vmp);
  put_vnode(vp);
  return(r);
}

/*===========================================================================*
 *				do_ftruncate				     *
 *===========================================================================*/
int do_ftruncate(void)
{
/* As with do_truncate(), truncate_vnode() does the actual work. */
  struct filp *rfilp;
  struct vnode *vp;
  int r, fd;
  off_t length;

  fd = job_m_in.m_lc_vfs_truncate.fd;

  length = job_m_in.m_lc_vfs_truncate.offset;
  if (length < 0) return(EINVAL);

  /* File is already opened; get a vnode pointer from filp */
  if ((rfilp = get_filp(fd, VNODE_WRITE)) == NULL)
	return(err_code);

  vp = rfilp->filp_vno;

  if (!(rfilp->filp_mode & W_BIT))
	r = EBADF;
  else if (S_ISREG(vp->v_mode) && vp->v_size == length)
	/* If the file size does not change, do not make the actual call. This
	 * ensures that the file times are retained when the file size remains
	 * the same, which is a POSIX requirement.
	 */
	r = OK;
  else
	r = truncate_vnode(vp, length);

  unlock_filp(rfilp);
  return(r);
}


/*===========================================================================*
 *				truncate_vnode				     *
 *===========================================================================*/
int
truncate_vnode(struct vnode *vp, off_t newsize)
{
/* Truncate a regular file or a pipe */
  int r;

  assert(tll_locked_by_me(&vp->v_lock));
  if (!S_ISREG(vp->v_mode) && !S_ISFIFO(vp->v_mode)) return(EINVAL);

  /* We must not compare the old and the new size here: this function may be
   * called for open(2), which requires an update to the file times if O_TRUNC
   * is given, even if the file size remains the same.
   */
  if ((r = req_ftrunc(vp->v_fs_e, vp->v_inode_nr, newsize, 0)) == OK)
	vp->v_size = newsize;
  return(r);
}


/*===========================================================================*
 *				do_symlinkat				     *
 *===========================================================================*/
int do_symlinkat(void)
{
/* Perform the symlinkat(name1, fd2, name2) system call: make 'name2', which
 * starts at fd2 if relative, a symbolic link with contents 'name1'.
 */
  int r;
  struct vnode *vp, *start;
  struct vmnt *vmp;
  char fullpath[PATH_MAX];
  struct lookup resolve;
  vir_bytes vname1, vname2;
  size_t vname1_length, vname2_length;

  lookup_init(&resolve, fullpath, PATH_NOFLAGS, &vmp, &vp);
  resolve.l_vmnt_lock = VMNT_WRITE;
  resolve.l_vnode_lock = VNODE_WRITE;

  vname1 = job_m_in.m_lc_vfs_linkat.name1;
  vname1_length = job_m_in.m_lc_vfs_linkat.len1;
  vname2 = job_m_in.m_lc_vfs_linkat.name2;
  vname2_length = job_m_in.m_lc_vfs_linkat.len2;

  if (vname1_length <= 1) return(ENOENT);
  if (vname1_length >= _POSIX_SYMLINK_MAX) return(ENAMETOOLONG);

  /* Get dir inode of 'name2' */
  if (fetch_name(vname2, vname2_length, fullpath) != OK) return(err_code);
  if ((r = get_start_dir(job_m_in.m_lc_vfs_linkat.fd2, fullpath,
	&start)) != OK)
	return(r);
  resolve.l_start = start;
  if ((vp = last_dir(&resolve, fp)) == NULL) {
	put_start_dir(start);
	return(err_code);
  }
  if ((r = forbidden(fp, vp, W_BIT|X_BIT)) == OK) {
	r = req_slink(vp->v_fs_e, vp->v_inode_nr, fullpath, who_e,
		      vname1, vname1_length - 1, fp->fp_effuid,
		      fp->fp_effgid);
  }

  unlock_vnode(vp);
  unlock_vmnt(vmp);
  put_vnode(vp);
  put_start_dir(start);

  return(r);
}

/*===========================================================================*
 *                              rdlink_direct                                *
 *===========================================================================*/
int
rdlink_direct(
	char *orig_path,
	char link_path[PATH_MAX], /* should have length PATH_MAX */
	struct fproc *rfp
)
{
/* Perform a readlink()-like call from within the VFS */
  int r;
  struct vnode *vp;
  struct vmnt *vmp;
  struct lookup resolve;

  lookup_init(&resolve, link_path, PATH_RET_SYMLINK, &vmp, &vp);
  resolve.l_vmnt_lock = VMNT_READ;
  resolve.l_vnode_lock = VNODE_READ;

  /* Temporarily open the file containing the symbolic link. Use link_path
   * for temporary storage to keep orig_path untouched. */
  strncpy(link_path, orig_path, PATH_MAX);	/* PATH_MAX includes '\0' */
  link_path[PATH_MAX - 1] = '\0';
  if ((vp = eat_path(&resolve, rfp)) == NULL) return(err_code);

  /* Make sure this is a symbolic link */
  if (!S_ISLNK(vp->v_mode))
	r = EINVAL;
  else
	r = req_rdlink(vp->v_fs_e, vp->v_inode_nr, NONE, (vir_bytes) link_path,
		       PATH_MAX - 1, 1);

  if (r > 0) link_path[r] = '\0';	/* Terminate string when succesful */

  unlock_vnode(vp);
  unlock_vmnt(vmp);
  put_vnode(vp);

  return r;
}

/*===========================================================================*
 *				do_readlinkat				     *
 *===========================================================================*/
int do_readlinkat(void)
{
/* Perform the readlinkat(dirfd, name, buf, bufsize) system call. */
  int r;
  struct vnode *vp, *start;
  struct vmnt *vmp;
  char fullpath[PATH_MAX];
  struct lookup resolve;
  vir_bytes vname;
  size_t vname_length, buf_size;
  vir_bytes buf;

  vname = job_m_in.m_lc_vfs_readlinkat.name;
  vname_length = job_m_in.m_lc_vfs_readlinkat.namelen;
  buf = job_m_in.m_lc_vfs_readlinkat.buf;
  buf_size = job_m_in.m_lc_vfs_readlinkat.bufsize;
  if (buf_size > SSIZE_MAX) return(EINVAL);

  lookup_init(&resolve, fullpath, PATH_RET_SYMLINK, &vmp, &vp);
  resolve.l_vmnt_lock = VMNT_READ;
  resolve.l_vnode_lock = VNODE_READ;

  /* Temporarily open the file containing the symbolic link */
  if (fetch_name(vname, vname_length, fullpath) != OK) return(err_code);
  if ((r = get_start_dir(job_m_in.m_lc_vfs_readlinkat.dirfd, fullpath,
	&start)) != OK)
	return(r);
  resolve.l_start = start;
  if ((vp = eat_path(&resolve, fp)) == NULL) {
	put_start_dir(start);
	return(err_code);
  }

  /* Make sure this is a symbolic link */
  if (!S_ISLNK(vp->v_mode))
	r = EINVAL;
  else
	r = req_rdlink(vp->v_fs_e, vp->v_inode_nr, who_e, buf, buf_size, 0);

  unlock_vnode(vp);
  unlock_vmnt(vmp);
  put_vnode(vp);
  put_start_dir(start);

  return(r);
}
