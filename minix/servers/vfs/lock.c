/* This file handles advisory file locking as required by POSIX.
 *
 * A lock belongs to a process (fp_tgid, the same in all of its threads) and
 * covers a byte range of a file.  A process's own locks never conflict with
 * each other: setting a lock replaces whatever the process held over that
 * range (upgrading, downgrading or splitting its locks), and its locks of the
 * same type that touch are merged.  Unlocking affects only the caller's locks.
 * Closing any descriptor of a file releases all of the process's locks on it.
 *
 * The entry points into this file are
 *   lock_op:	perform locking operations for FCNTL system call
 *   lock_release: release a process's locks on a file (on close)
 *   lock_revive: revive processes when a lock is released
 */

#include "fs.h"
#include <minix/com.h>
#include <minix/u64.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include "file.h"
#include "lock.h"
#include "vnode.h"

static int lock_conflict(struct file_lock *flp, struct vnode *vp, pid_t owner,
	int ltype, off_t first, off_t last);
static int lock_clear(struct vnode *vp, pid_t owner, off_t first, off_t last);
static struct file_lock *lock_alloc(void);

/*===========================================================================*
 *				lock_op					     *
 *===========================================================================*/
int lock_op(int fd, int req, vir_bytes arg)
{
/* Perform the advisory locking required by POSIX. */
  int r, ltype, need;
  mode_t mo;
  off_t first, last;
  struct filp *f;
  struct vnode *vp;
  struct flock flock;
  struct file_lock *flp, *conflict;
  pid_t owner;

  assert(req == F_GETLK || req == F_SETLK || req == F_SETLKW);

  f = fp->fp_fd->fd_filp[fd];
  assert(f != NULL);
  vp = f->filp_vno;
  owner = fp->fp_tgid;

  /* Fetch the flock structure from user space. */
  r = sys_datacopy_wrapper(who_e, arg, VFS_PROC_NR, (vir_bytes)&flock,
      sizeof(flock));
  if (r != OK) return(EINVAL);

  /* Make some error checks. */
  ltype = flock.l_type;
  mo = f->filp_mode;
  if (ltype != F_UNLCK && ltype != F_RDLCK && ltype != F_WRLCK) return(EINVAL);
  if (req == F_GETLK && ltype == F_UNLCK) return(EINVAL);
  if (!S_ISREG(vp->v_mode) && !S_ISBLK(vp->v_mode))
	return(EINVAL);
  if (req != F_GETLK && ltype == F_RDLCK && (mo & R_BIT) == 0) return(EBADF);
  if (req != F_GETLK && ltype == F_WRLCK && (mo & W_BIT) == 0) return(EBADF);

  /* Compute the first and last bytes in the lock region. */
  switch (flock.l_whence) {
    case SEEK_SET:	first = 0; break;
    case SEEK_CUR:	first = f->filp_pos; break;
    case SEEK_END:	first = vp->v_size; break;
    default:	return(EINVAL);
  }

  /* Check for overflow. */
  if ((flock.l_start > 0) && ((first + flock.l_start) < first))
	return(EOVERFLOW);
  if ((flock.l_start < 0) && ((first + flock.l_start) > first))
	return(EOVERFLOW);
  first = first + flock.l_start;
  if (first < 0) return(EINVAL);
  if (flock.l_len > 0) {
	last = first + flock.l_len - 1;
	if (last < first) return(EOVERFLOW);
  } else if (flock.l_len < 0) {
	/* A negative length covers the bytes before l_start (POSIX). */
	last = first - 1;
	first = first + flock.l_len;
	if (first < 0) return(EINVAL);
  } else
	last = MAX_FILE_POS;

  /* Look for a lock of another process that conflicts. */
  conflict = NULL;
  if (ltype != F_UNLCK) {
	for (flp = &file_lock[0]; flp < &file_lock[NR_LOCKS]; flp++) {
		if (lock_conflict(flp, vp, owner, ltype, first, last)) {
			conflict = flp;
			break;
		}
	}
  }

  if (req == F_GETLK) {
	if (conflict != NULL) {
		/* Report on the conflicting lock. */
		flock.l_type = conflict->lock_type;
		flock.l_whence = SEEK_SET;
		flock.l_start = conflict->lock_first;
		flock.l_len = (conflict->lock_last == MAX_FILE_POS) ? 0 :
		    conflict->lock_last - conflict->lock_first + 1;
		flock.l_pid = conflict->lock_pid;
	} else {
		flock.l_type = F_UNLCK;
	}

	/* Copy the flock structure back to the caller. */
	r = sys_datacopy_wrapper(VFS_PROC_NR, (vir_bytes)&flock, who_e, arg,
	    sizeof(flock));
	return(r);
  }

  if (conflict != NULL) {
	if (req == F_SETLK)
		return(EAGAIN);

	/* F_SETLKW: wait until some lock is released, then try again. */
	fp->fp_flock.fd = fd;
	fp->fp_flock.cmd = req;
	fp->fp_flock.arg = arg;
	suspend(FP_BLOCKED_ON_FLOCK);
	return(SUSPEND);
  }

  /* No conflict.  The new lock replaces whatever the caller held over the
   * range.  Make sure the table has room first, so that a failure leaves the
   * caller's locks as they were: clearing the range can split one of its
   * locks in two, and the new lock takes a slot.
   */
  need = (ltype != F_UNLCK);
  for (flp = &file_lock[0]; flp < &file_lock[NR_LOCKS]; flp++) {
	if (flp->lock_type != 0 && flp->lock_vnode == vp &&
	    flp->lock_pid == owner && flp->lock_first < first &&
	    flp->lock_last > last) {
		need++;		/* this one gets split */
		break;
	}
  }
  if (NR_LOCKS - nr_locks < need) return(ENOLCK);

  if (lock_clear(vp, owner, first, last))
	lock_revive();	/* a lock was released or weakened */

  if (ltype == F_UNLCK) return(OK);

  /* Merge with the caller's locks of the same type that touch the range;
   * after lock_clear() none of them overlap it. */
  for (flp = &file_lock[0]; flp < &file_lock[NR_LOCKS]; flp++) {
	if (flp->lock_type != ltype || flp->lock_vnode != vp ||
	    flp->lock_pid != owner)
		continue;
	if (flp->lock_last != MAX_FILE_POS && flp->lock_last + 1 == first) {
		first = flp->lock_first;
	} else if (last != MAX_FILE_POS && last + 1 == flp->lock_first) {
		last = flp->lock_last;
	} else
		continue;
	flp->lock_type = 0;
	nr_locks--;
  }

  flp = lock_alloc();
  assert(flp != NULL);	/* room was checked above */
  flp->lock_type = ltype;
  flp->lock_pid = owner;
  flp->lock_vnode = vp;
  flp->lock_first = first;
  flp->lock_last = last;
  return(OK);
}

/*===========================================================================*
 *				lock_conflict				     *
 *===========================================================================*/
static int lock_conflict(struct file_lock *flp, struct vnode *vp, pid_t owner,
	int ltype, off_t first, off_t last)
{
/* Does lock table entry 'flp' keep 'owner' from setting a lock of type
 * 'ltype' on bytes 'first' to 'last' of 'vp'?
 */
  if (flp->lock_type == 0) return(FALSE);	/* unused slot */
  if (flp->lock_vnode != vp) return(FALSE);	/* different file */
  if (flp->lock_pid == owner) return(FALSE);	/* the caller's own */
  if (last < flp->lock_first || first > flp->lock_last)
	return(FALSE);				/* no overlap */
  if (ltype == F_RDLCK && flp->lock_type == F_RDLCK)
	return(FALSE);				/* shared locks */
  return(TRUE);
}

/*===========================================================================*
 *				lock_clear				     *
 *===========================================================================*/
static int lock_clear(struct vnode *vp, pid_t owner, off_t first, off_t last)
{
/* Remove bytes 'first' to 'last' of 'vp' from the locks of 'owner': delete,
 * trim or split them.  The caller has made sure a split finds a free slot.
 * Return whether anything was removed.
 */
  struct file_lock *flp, *flp2;
  int changed = FALSE;

  for (flp = &file_lock[0]; flp < &file_lock[NR_LOCKS]; flp++) {
	if (flp->lock_type == 0 || flp->lock_vnode != vp ||
	    flp->lock_pid != owner)
		continue;
	if (last < flp->lock_first || first > flp->lock_last)
		continue;			/* no overlap */

	changed = TRUE;
	if (first <= flp->lock_first && last >= flp->lock_last) {
		flp->lock_type = 0;		/* covered entirely */
		nr_locks--;
	} else if (first <= flp->lock_first) {
		flp->lock_first = last + 1;	/* front removed */
	} else if (last >= flp->lock_last) {
		flp->lock_last = first - 1;	/* back removed */
	} else {
		/* The middle removed: split in two. */
		flp2 = lock_alloc();
		assert(flp2 != NULL);
		flp2->lock_type = flp->lock_type;
		flp2->lock_pid = flp->lock_pid;
		flp2->lock_vnode = flp->lock_vnode;
		flp2->lock_first = last + 1;
		flp2->lock_last = flp->lock_last;
		flp->lock_last = first - 1;
	}
  }

  return(changed);
}

/*===========================================================================*
 *				lock_alloc				     *
 *===========================================================================*/
static struct file_lock *lock_alloc(void)
{
/* Take a free lock table slot, or return NULL if there is none. */
  struct file_lock *flp;

  for (flp = &file_lock[0]; flp < &file_lock[NR_LOCKS]; flp++) {
	if (flp->lock_type == 0) {
		nr_locks++;
		return(flp);
	}
  }
  return(NULL);
}

/*===========================================================================*
 *				lock_release				     *
 *===========================================================================*/
void lock_release(struct fproc *rfp, struct vnode *vp)
{
/* A descriptor of 'vp' was closed: release all of the process's locks on it,
 * as POSIX requires (whichever descriptor set them).
 */
  if (nr_locks == 0) return;

  if (lock_clear(vp, rfp->fp_tgid, 0, MAX_FILE_POS))
	lock_revive();
}

/*===========================================================================*
 *				lock_revive				     *
 *===========================================================================*/
void
lock_revive(void)
{
/* Go find all the processes that are waiting for any kind of lock and
 * revive them all.  The ones that are still blocked will block again when
 * they run.  The others will complete.  This strategy is a space-time
 * tradeoff.  Figuring out exactly which ones to unblock now would take
 * extra code, and the only thing it would win would be some performance in
 * extremely rare circumstances (namely, that somebody actually used
 * locking).
 */

  struct fproc *fptr;

  for (fptr = &fproc[0]; fptr < &fproc[NR_PROCS]; fptr++){
	if (fptr->fp_pid == PID_FREE) continue;
	if (fptr->fp_blocked_on == FP_BLOCKED_ON_FLOCK) {
		revive(fptr->fp_endpoint, 0);
	}
  }
}
