/* This file contains the procedures for creating, opening, closing, and
 * seeking on files.
 *
 * The entry points into this file are
 *   do_openat:	perform the OPENAT system call
 *   do_mknodat: perform the MKNODAT system call
 *   do_mkdirat: perform the MKDIRAT system call
 *   do_close:	perform the CLOSE system call
 *   do_lseek:  perform the LSEEK system call
 */

#include "fs.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <minix/callnr.h>
#include <minix/com.h>
#include <minix/u64.h>
#include "file.h"
#include "lock.h"
#include <sys/dirent.h>
#include <assert.h>
#include <minix/vfsif.h>
#include "vnode.h"
#include "vmnt.h"
#include "path.h"

static char mode_map[] = {R_BIT, W_BIT, R_BIT|W_BIT, 0};

static struct vnode *new_node(struct lookup *resolve, int oflags,
	mode_t bits);
static int pipe_open(int fd, struct vnode *vp, mode_t bits, int oflags);

/*===========================================================================*
 *				do_openat				     *
 *===========================================================================*/
int do_openat(void)
{
/* Perform the openat(dirfd, name, flags[, mode]) system call; open(2) is
 * openat(AT_FDCWD, ...).  The syscall might provide 'name' embedded in the
 * message.
 */
  int r;
  struct vnode *start;
  char fullpath[PATH_MAX];

  if (copy_pathat(fullpath, sizeof(fullpath)) != OK)
	return(err_code);
  if ((r = get_start_dir(job_m_in.m_lc_vfs_pathat.dirfd, fullpath,
	&start)) != OK)
	return(r);

  r = common_open(fullpath, job_m_in.m_lc_vfs_pathat.flags,
	job_m_in.m_lc_vfs_pathat.mode, FALSE /*for_exec*/, start);

  put_start_dir(start);
  return(r);
}

/*===========================================================================*
 *				common_open				     *
 *===========================================================================*/
int common_open(char path[PATH_MAX], int oflags, mode_t omode, int for_exec,
	struct vnode *start_dir)
{
/* Open 'path', which starts at 'start_dir' if it is relative (NULL: the
 * working directory).  Used by openat(2), exec and core dumps. */
  int r, exist = TRUE;
  devmajor_t major_dev;
  dev_t dev;
  mode_t bits;
  struct filp *filp;
  struct vnode *vp;
  struct vmnt *vmp;
  struct dmap *dp;
  struct lookup resolve;
  int fd, start = 0;

  /* Remap the bottom two bits of oflags. */
  bits = (mode_t) mode_map[oflags & O_ACCMODE];
  if (!bits) return(EINVAL);

  /* O_DIRECTORY opens only a directory, which O_CREAT cannot make. */
  if ((oflags & (O_CREAT | O_DIRECTORY)) == (O_CREAT | O_DIRECTORY))
	return(EINVAL);

  /* See if file descriptor and filp slots are available. */
  if ((r = get_fd(fp, start, bits, &fd, &filp)) != OK)
	return(r);

  /* With O_NOFOLLOW the lookup stops at a final symlink, which then fails
   * the open below. */
  lookup_init(&resolve, path,
	(oflags & O_NOFOLLOW) ? PATH_RET_SYMLINK : PATH_NOFLAGS, &vmp, &vp);
  resolve.l_start = start_dir;

  /* If O_CREATE is set, try to make the file. */
  if (oflags & O_CREAT) {
        omode = I_REGULAR | (omode & ALLPERMS & fp->fp_fd->fd_umask);
	vp = new_node(&resolve, oflags, omode);
	r = err_code;
	if (r == OK) exist = FALSE;	/* We just created the file */
	else if (r != EEXIST) {		/* other error */
		if (vp) unlock_vnode(vp);
		unlock_filp(filp);
		return(r);
	}
	else exist = !(oflags & O_EXCL);/* file exists, if the O_EXCL
					   flag is set this is an error */
  } else {
	/* Scan path name */
	resolve.l_vmnt_lock = VMNT_READ;
	resolve.l_vnode_lock = VNODE_OPCL;
	if ((vp = eat_path(&resolve, fp)) == NULL) {
		unlock_filp(filp);
		return(err_code);
	}

	if (vmp != NULL) unlock_vmnt(vmp);
  }

  /* Claim the file descriptor and filp slot and fill them in. */
  fp->fp_fd->fd_filp[fd] = filp;
  filp->filp_count = 1;
  filp->filp_vno = vp;
  filp->filp_flags = oflags;
  if (oflags & O_CLOEXEC)
	FD_SET(fd, &fp->fp_fd->fd_cloexec_set);

  /* Only do the normal open code if we didn't just create the file. */
  if (exist) {
	/* Check permissions based on the given open flags, except when we are
	 * opening an executable for the purpose of passing a file descriptor
	 * to its interpreter for execution, in which case we check the X bit.
	 * A symlink here means O_NOFOLLOW stopped at it.
	 */
	if (S_ISLNK(vp->v_mode))
		r = ELOOP;
	else if ((oflags & O_DIRECTORY) && !S_ISDIR(vp->v_mode))
		r = ENOTDIR;
	else if ((r = forbidden(fp, vp, for_exec ? X_BIT : bits)) == OK) {
		/* Opening reg. files, directories, and special files differ */
		switch (vp->v_mode & S_IFMT) {
		   case S_IFREG:
			/* Truncate regular file if O_TRUNC. */
			if (oflags & O_TRUNC) {
				if ((r = forbidden(fp, vp, W_BIT)) != OK)
					break;
				upgrade_vnode_lock(vp);
				truncate_vnode(vp, 0);
			}
			break;
		   case S_IFDIR:
			/* Directories may be read but not written. */
			r = (bits & W_BIT ? EISDIR : OK);
			break;
		   case S_IFCHR:
			/* Invoke the driver for special processing. */
			dev = vp->v_sdev;
			/* TTY needs to know about the O_NOCTTY flag. */
			r = cdev_open(fd, dev, bits | (oflags & O_NOCTTY));
			vp = filp->filp_vno;	/* Might be updated by
						 * cdev_open after cloning */
			break;
		   case S_IFBLK:

			lock_bsf();

			/* Invoke the driver for special processing. */
			dev = vp->v_sdev;
			r = bdev_open(dev, bits);
			if (r != OK) {
				unlock_bsf();
				break;
			}

			major_dev = major(vp->v_sdev);
			dp = &dmap[major_dev];
			if (dp->dmap_driver == NONE) {
				printf("VFS: block driver disappeared!\n");
				unlock_bsf();
				r = ENXIO;
				break;
			}

			/* Check whether the device is mounted or not. If so,
			 * then that FS is responsible for this device.
			 * Otherwise we default to ROOT_FS.
			 */
			vp->v_bfs_e = ROOT_FS_E; /* By default */
			for (vmp = &vmnt[0]; vmp < &vmnt[NR_MNTS]; ++vmp)
				if (vmp->m_dev == vp->v_sdev &&
				    !(vmp->m_flags & VMNT_FORCEROOTBSF)) {
					vp->v_bfs_e = vmp->m_fs_e;
				}

			/* Send the driver label to the file system that will
			 * handle the block I/O requests (even when its label
			 * and endpoint are known already), but only when it is
			 * the root file system. Other file systems will
			 * already have it anyway.
			 */
			if (vp->v_bfs_e != ROOT_FS_E) {
				unlock_bsf();
				break;
			}

			if (req_newdriver(vp->v_bfs_e, vp->v_sdev,
					dp->dmap_label) != OK) {
				printf("VFS: error sending driver label\n");
				bdev_close(dev);
				r = ENXIO;
			}
			unlock_bsf();
			break;

		   case S_IFIFO:
			/* Create a mapped inode on PFS which handles reads
			   and writes to this named pipe. */
			upgrade_vnode_lock(vp);
			r = map_vnode(vp, PFS_PROC_NR);
			if (r == OK) {
				if (vp->v_ref_count == 1) {
					if (vp->v_size != 0)
						r = truncate_vnode(vp, 0);
				}
				oflags |= O_APPEND;	/* force append mode */
				filp->filp_flags = oflags;
			}
			/* Each opener keeps a filp of its own.  (This used to
			 * share an existing reader's or writer's filp, for its
			 * file position, which FIFOs do not use, and set its
			 * flags, clobbering the other opener's O_NONBLOCK.)
			 */
			if (r == OK) {
				r = pipe_open(fd, vp, bits, oflags);
			}
			break;
		   case S_IFSOCK:
			r = EOPNOTSUPP;
			break;
		   default:
			printf("VFS: attempt to open file <%llu,%llu> of "
			    "type 0%o\n", (unsigned long long)vp->v_dev, (unsigned long long)vp->v_inode_nr,
			    vp->v_mode & S_IFMT);
			r = EIO;
		}
	}
  }

  unlock_filp(filp);

  /* If error, release inode, and the fd with its close-on-exec flag. */
  if (r != OK) {
	if (r != SUSPEND) {
		fp->fp_fd->fd_filp[fd] = NULL;
		FD_CLR(fd, &fp->fp_fd->fd_cloexec_set);
		filp->filp_count = 0;
		filp->filp_vno = NULL;
		put_vnode(vp);
	}
  } else {
	r = fd;
  }

  return(r);
}


/*===========================================================================*
 *				new_node				     *
 *===========================================================================*/
static struct vnode *new_node(struct lookup *resolve, int oflags, mode_t bits)
{
/* Try to create a new inode and return a pointer to it. If the inode already
   exists, return a pointer to it as well, but set err_code accordingly.
   NULL is returned if the path cannot be resolved up to the last
   directory, or when the inode cannot be created due to permissions or
   otherwise. */
  struct vnode *dirp, *vp;
  struct vmnt *dir_vmp, *vp_vmp;
  int r;
  struct node_details res;
  struct lookup findnode;
  char *path;

  path = resolve->l_path;	/* For easy access */

  lookup_init(&findnode, path, resolve->l_flags, &dir_vmp, &dirp);
  findnode.l_start = resolve->l_start;
  findnode.l_vmnt_lock = VMNT_WRITE;
  findnode.l_vnode_lock = VNODE_WRITE; /* dir node */

  /* When O_CREAT and O_EXCL flags are set, the path may not be named by a
   * symbolic link. */
  if (oflags & O_EXCL) findnode.l_flags |= PATH_RET_SYMLINK;

  /* See if the path can be opened down to the last directory. */
  if ((dirp = last_dir(&findnode, fp)) == NULL) return(NULL);

  /* The final directory is accessible. Get final component of the path. */
  lookup_init(&findnode, findnode.l_path, findnode.l_flags, &vp_vmp, &vp);
  findnode.l_vmnt_lock = VMNT_WRITE;
  findnode.l_vnode_lock = (oflags & O_TRUNC) ? VNODE_WRITE : VNODE_OPCL;
  vp = advance(dirp, &findnode, fp);
  assert(vp_vmp == NULL);	/* Lookup to last dir should have yielded lock
				 * on vmp or final component does not exist.
				 * Either way, vp_vmp ought to be not set.
				 */

  /* The combination of a symlink with absolute path followed by a danglink
   * symlink results in a new path that needs to be re-resolved entirely. */
  if (path[0] == '/') {
	unlock_vnode(dirp);
	unlock_vmnt(dir_vmp);
	put_vnode(dirp);
	if (vp != NULL) {
		unlock_vnode(vp);
		put_vnode(vp);
	}
	return new_node(resolve, oflags, bits);
  }

  if (vp == NULL && err_code == ENOENT) {
	/* Last path component does not exist. Make a new directory entry. */
	if ((vp = get_free_vnode()) == NULL) {
		/* Can't create new entry: out of vnodes. */
		unlock_vnode(dirp);
		unlock_vmnt(dir_vmp);
		put_vnode(dirp);
		return(NULL);
	}

	lock_vnode(vp, VNODE_OPCL);
	upgrade_vmnt_lock(dir_vmp); /* Creating file, need exclusive access */

	if ((r = forbidden(fp, dirp, W_BIT|X_BIT)) != OK ||
	    (r = req_create(dirp->v_fs_e, dirp->v_inode_nr,bits, fp->fp_effuid,
			    fp->fp_effgid, path, &res)) != OK ) {
		/* Can't create inode either due to permissions or some other
		 * problem. In case r is EEXIST, we might be dealing with a
		 * dangling symlink.*/

		/* Downgrade lock to prevent deadlock during symlink resolving*/
		downgrade_vmnt_lock(dir_vmp);

		if (r == EEXIST) {
			struct vnode *slp, *old_start;


			/* Resolve path up to symlink */
			findnode.l_flags = PATH_RET_SYMLINK;
			findnode.l_vnode_lock = VNODE_READ;
			findnode.l_vnode = &slp;
			slp = advance(dirp, &findnode, fp);
			if (slp != NULL) {
				if (S_ISLNK(slp->v_mode)) {
					/* Get contents of link */

					r = req_rdlink(slp->v_fs_e,
						       slp->v_inode_nr,
						       VFS_PROC_NR,
						       (vir_bytes) path,
						       PATH_MAX - 1, 0);
					if (r < 0) {
						/* Failed to read link */
						unlock_vnode(slp);
						unlock_vnode(dirp);
						unlock_vmnt(dir_vmp);
						put_vnode(slp);
						put_vnode(dirp);
						err_code = r;
						return(NULL);
					}
					path[r] = '\0'; /* Terminate path */
				}
				unlock_vnode(slp);
				put_vnode(slp);
			}

			/* Try to create the inode the dangling symlink was
			 * pointing to. We have to use dirp as starting point
			 * as there might be multiple successive symlinks
			 * crossing multiple mountpoints.
			 * Unlock vnodes and vmnts as we're going to recurse.
			 */
			unlock_vnode(dirp);
			unlock_vnode(vp);
			unlock_vmnt(dir_vmp);

			old_start = resolve->l_start;
			resolve->l_start = dirp;
			vp = new_node(resolve, oflags, bits);
			resolve->l_start = old_start;

			if (vp != NULL) {
				put_vnode(dirp);
				*(resolve->l_vnode) = vp;
				return(vp);
			}
			r = err_code;
		}

		if (r == EEXIST)
			err_code = EIO; /* Impossible, we have verified that
					 * the last component doesn't exist and
					 * is not a dangling symlink. */
		else
			err_code = r;

		unlock_vmnt(dir_vmp);
		unlock_vnode(dirp);
		unlock_vnode(vp);
		put_vnode(dirp);
		return(NULL);
	}

	/* Store results and mark vnode in use */

	vp->v_fs_e = res.fs_e;
	vp->v_inode_nr = res.inode_nr;
	vp->v_mode = res.fmode;
	vp->v_size = res.fsize;
	vp->v_uid = res.uid;
	vp->v_gid = res.gid;
	vp->v_sdev = res.dev;
	vp->v_vmnt = dirp->v_vmnt;
	vp->v_dev = vp->v_vmnt->m_dev;
	vp->v_fs_count = 1;
	vp->v_ref_count = 1;
  } else {
	/* Either last component exists, or there is some other problem. */
	if (vp != NULL) {
		r = EEXIST;	/* File exists or a symlink names a file while
				 * O_EXCL is set. */
	} else
		r = err_code;	/* Other problem. */
  }

  err_code = r;
  /* When dirp equals vp, we shouldn't release the lock as a vp is locked only
   * once. Releasing the lock would cause the resulting vp not be locked and
   * cause mayhem later on. */
  if (dirp != vp) {
	unlock_vnode(dirp);
  }
  unlock_vmnt(dir_vmp);
  put_vnode(dirp);

  *(resolve->l_vnode) = vp;
  return(vp);
}


/*===========================================================================*
 *				pipe_open				     *
 *===========================================================================*/
static int pipe_open(int fd, struct vnode *vp, mode_t bits, int oflags)
{
/*  This function is called from common_open. It checks if
 *  there is at least one reader/writer pair for the pipe, if not
 *  it suspends the caller, otherwise it revives all other blocked
 *  processes hanging on the pipe.
 */

  /* Opened for both reading and writing, the caller is both ends: it never
   * waits, and wakes up anyone who was waiting for either end.  POSIX leaves
   * this undefined; the BSDs and Linux allow it.
   */
  if ((bits & (R_BIT|W_BIT)) == (R_BIT|W_BIT)) {
	if (susp_count > 0)
		release(vp, VFS_OPENAT, susp_count);
	return(OK);
  }

  /* Find the reader/writer at the other end of the pipe */
  if (find_filp(vp, bits & W_BIT ? R_BIT : W_BIT) == NULL) {
	/* Not found */
	if (oflags & O_NONBLOCK) {
		if (bits & W_BIT) return(ENXIO);
	} else {
		/* Let's wait for the other side to show up */
		fp->fp_popen.fd = fd;
		suspend(FP_BLOCKED_ON_POPEN);
		return(SUSPEND);
	}
  } else if (susp_count > 0) { /* revive blocked processes */
	release(vp, VFS_OPENAT, susp_count);
  }
  return(OK);
}


/*===========================================================================*
 *				do_mknodat				     *
 *===========================================================================*/
int do_mknodat(void)
{
/* Perform the mknodat(dirfd, name, mode, addr) system call. */
  register mode_t bits, mode_bits;
  int r;
  struct vnode *vp, *start;
  struct vmnt *vmp;
  char fullpath[PATH_MAX];
  struct lookup resolve;
  vir_bytes vname1;
  size_t vname1_length;
  dev_t dev;

  vname1 = job_m_in.m_lc_vfs_mknodat.name;
  vname1_length = job_m_in.m_lc_vfs_mknodat.len;
  mode_bits = job_m_in.m_lc_vfs_mknodat.mode;
  dev = job_m_in.m_lc_vfs_mknodat.device;

  /* If the path names a symbolic link, mknod() shall fail with EEXIST. */
  lookup_init(&resolve, fullpath, PATH_RET_SYMLINK, &vmp, &vp);
  resolve.l_vmnt_lock = VMNT_WRITE;
  resolve.l_vnode_lock = VNODE_WRITE;

  /* Only the super_user may make nodes other than fifos. */
  if (!super_user && !S_ISFIFO(mode_bits))
	return(EPERM);

  bits = (mode_bits & S_IFMT) | (mode_bits & ACCESSPERMS & fp->fp_fd->fd_umask);

  /* Open directory that's going to hold the new node. */
  if (fetch_name(vname1, vname1_length, fullpath) != OK) return(err_code);
  if ((r = get_start_dir(job_m_in.m_lc_vfs_mknodat.dirfd, fullpath,
	&start)) != OK)
	return(r);
  resolve.l_start = start;
  vp = last_dir(&resolve, fp);
  if (vp == NULL) {
	put_start_dir(start);
	return(err_code);
  }

  /* Make sure that the object is a directory */
  if (!S_ISDIR(vp->v_mode)) {
	r = ENOTDIR;
  } else if ((r = forbidden(fp, vp, W_BIT|X_BIT)) == OK) {
	r = req_mknod(vp->v_fs_e, vp->v_inode_nr, fullpath, fp->fp_effuid,
		      fp->fp_effgid, bits, dev);
  }

  unlock_vnode(vp);
  unlock_vmnt(vmp);
  put_vnode(vp);
  put_start_dir(start);
  return(r);
}

/*===========================================================================*
 *				do_mkdirat				     *
 *===========================================================================*/
int do_mkdirat(void)
{
/* Perform the mkdirat(dirfd, name, mode) system call.  The syscall might
 * provide 'name' embedded in the message. */
  mode_t bits;			/* mode bits for the new inode */
  int r;
  struct vnode *vp, *start;
  struct vmnt *vmp;
  char fullpath[PATH_MAX];
  struct lookup resolve;
  mode_t dirmode;

  if (copy_pathat(fullpath, sizeof(fullpath)) != OK)
	return(err_code);
  dirmode = job_m_in.m_lc_vfs_pathat.mode;

  lookup_init(&resolve, fullpath, PATH_NOFLAGS, &vmp, &vp);
  resolve.l_vmnt_lock = VMNT_WRITE;
  resolve.l_vnode_lock = VNODE_WRITE;

  bits = I_DIRECTORY | (dirmode & RWX_MODES & fp->fp_fd->fd_umask);
  if ((r = get_start_dir(job_m_in.m_lc_vfs_pathat.dirfd, fullpath,
	&start)) != OK)
	return(r);
  resolve.l_start = start;
  vp = last_dir(&resolve, fp);
  if (vp == NULL) {
	put_start_dir(start);
	return(err_code);
  }

  /* Make sure that the object is a directory */
  if (!S_ISDIR(vp->v_mode)) {
	r = ENOTDIR;
  } else if ((r = forbidden(fp, vp, W_BIT|X_BIT)) == OK) {
	r = req_mkdir(vp->v_fs_e, vp->v_inode_nr, fullpath, fp->fp_effuid,
		      fp->fp_effgid, bits);
  }

  unlock_vnode(vp);
  unlock_vmnt(vmp);
  put_vnode(vp);
  put_start_dir(start);
  return(r);
}

/*===========================================================================*
 *				actual_lseek				     *
 *===========================================================================*/
int actual_lseek(struct fproc *rfp, int seekfd, int seekwhence, off_t offset,
	off_t *newposp)
{
  register struct filp *rfilp;
  int r = OK;
  off_t pos, newpos;

  /* Check to see if the file descriptor is valid. */
  if ( (rfilp = get_filp2(rfp, seekfd, VNODE_READ)) == NULL) {
	return(err_code);
  }

  /* No lseek on pipes. */
  if (S_ISFIFO(rfilp->filp_vno->v_mode)) {
	unlock_filp(rfilp);
	return(ESPIPE);
  }

  /* The value of 'whence' determines the start position to use. */
  switch(seekwhence) {
    case SEEK_SET: pos = 0; break;
    case SEEK_CUR: pos = rfilp->filp_pos; break;
    case SEEK_END: pos = rfilp->filp_vno->v_size; break;
    default: unlock_filp(rfilp); return(EINVAL);
  }

  newpos = pos + offset;

  /* Check for overflow. */
  if ((offset > 0) && (newpos <= pos)) {
	r = EOVERFLOW;
  } else if ((offset < 0) && (newpos >= pos)) {
	r = EOVERFLOW;
  } else {
	if (newposp != NULL) *newposp = newpos;

	if (newpos != rfilp->filp_pos) {
		rfilp->filp_pos = newpos;

		/* Inhibit read ahead request */
		r = req_inhibread(rfilp->filp_vno->v_fs_e,
				  rfilp->filp_vno->v_inode_nr);
	}
  }

  unlock_filp(rfilp);
  return(r);
}

/*===========================================================================*
 *				do_lseek				     *
 *===========================================================================*/
int do_lseek(void)
{
  /* Perform the lseek(2) system call. */
  off_t newpos = 0;
  int r;

  if ((r = actual_lseek(fp, job_m_in.m_lc_vfs_lseek.fd,
		job_m_in.m_lc_vfs_lseek.whence, job_m_in.m_lc_vfs_lseek.offset,
		&newpos)) != OK)
	return r;

  /* insert the new position into the output message */
  job_m_out.m_vfs_lc_lseek.offset = newpos;
  return OK;
}

/*===========================================================================*
 *				do_close				     *
 *===========================================================================*/
int do_close(void)
{
/* Perform the close(fd) or closenb(fd) system call. */
  int fd, nblock;

  fd = job_m_in.m_lc_vfs_close.fd;
  nblock = job_m_in.m_lc_vfs_close.nblock;

  return close_fd(fp, fd, !nblock /*may_suspend*/);
}


/*===========================================================================*
 *				close_fd				     *
 *===========================================================================*/
int
close_fd(struct fproc * rfp, int fd_nr, int may_suspend)
{
/* Perform the close(fd) system call. */
  register struct filp *rfilp;
  register struct vnode *vp;
  int r;

  /* First locate the vnode that belongs to the file descriptor. */
  if ( (rfilp = get_filp2(rfp, fd_nr, VNODE_OPCL)) == NULL) return(err_code);

  vp = rfilp->filp_vno;

  /* first, make all future get_filp2()'s fail; otherwise
   * we might try to close the same fd in different threads
   */
  rfp->fp_fd->fd_filp[fd_nr] = NULL;

  /* Knotes on the descriptor go with it (kqueue(2)). */
  kq_fd_closed(rfp->fp_fd, fd_nr);

  /* Release the process's locks on the file, if any, while the filp still
   * holds its vnode: close_filp() may put the last reference. */
  lock_release(rfp, vp);

  r = close_filp(rfilp, may_suspend);

  FD_CLR(fd_nr, &rfp->fp_fd->fd_cloexec_set);

  return(r);
}
