/* This file implements kqueue(2) and kevent(2).
 *
 * A kqueue is a file descriptor on an anonymous pipe-file-system node, with a
 * struct kqueue hanging off its filp.  Its knotes name what the process wants
 * to hear about:
 *  - EVFILT_READ and EVFILT_WRITE on a descriptor.  For pipes, sockets and
 *    devices, readiness comes from the select machinery (select.c), which
 *    a waiting kevent call uses as a select call would: no driver protocol
 *    of its own.  For regular files, readiness is the BSD one: readable while
 *    the file position is below the size (data: the bytes left), always
 *    writable; writes to the file wake up waiters.
 *  - EVFILT_VNODE, for NOTE_WRITE and NOTE_EXTEND on a regular file.
 *  - EVFILT_TIMER, periodic or one-shot, in milliseconds.
 *  - EVFILT_USER, triggered by NOTE_TRIGGER.
 * Other filters fail with EINVAL.
 *
 * Readiness is level-triggered and evaluated anew whenever events are
 * collected; EV_CLEAR is honoured where there is state to clear (user events,
 * timers, vnode notes) and otherwise behaves as level-triggered, which the
 * applications have to cope with anyway (spurious readiness is allowed).
 *
 * As on the BSDs, a kqueue belongs to the descriptor table it was created in:
 * a child process inheriting the descriptor cannot use it (EBADF), and
 * closing a descriptor removes its knotes.
 */

#include "fs.h"
#include <sys/event.h>
#include <sys/stat.h>
#include <minix/callnr.h>
#include <minix/com.h>
#include <minix/u64.h>
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "file.h"
#include "vnode.h"

#define KQ_MAXKNOTES	OPEN_MAX * 4	/* knotes per kqueue */
#define KQ_CHUNK	32		/* kevents copied in or out at once */

struct knote {
  uintptr_t kn_ident;
  uint32_t kn_filter;
  uint32_t kn_flags;		/* EV_* as given, minus actions */
  uint32_t kn_fflags;
  int64_t kn_data;
  intptr_t kn_udata;
  int kn_disabled;
  int kn_triggered;		/* EVFILT_USER */
  struct filp *kn_filp;		/* EVFILT_READ, _WRITE, _VNODE */
  clock_t kn_expire;		/* EVFILT_TIMER: next expiry (uptime) */
  clock_t kn_period;		/* EVFILT_TIMER: in ticks; 0 if one-shot */
  uint32_t kn_pending;		/* EVFILT_VNODE: notes not yet reported */
  int kn_watch;			/* counted in kq_watchers */
};

struct kqueue {
  struct kqueue *kq_next;	/* on the list of all kqueues */
  struct filedesc *kq_fdtab;	/* descriptor table it belongs to */
  struct knote *kq_knotes;	/* the knotes, kq_nknotes of them */
  int kq_nknotes;
  int kq_maxknotes;		/* room in kq_knotes */
  int kq_busy;			/* do_kevent() calls using it */
  int kq_dead;			/* closed meanwhile: free when not busy */
};

static struct kqueue *kq_list;
static int kq_watchers;		/* knotes waiting for writes to files */

/* Is clock time 'a' before 'b'?  Uptimes wrap, and clock_t is unsigned and
 * may be narrower than long.
 */
static int kq_before(clock_t a, clock_t b)
{
  return (clock_t) (a - b) > ((clock_t) -1 >> 1);
}

static int kq_is_fdfilter(uint32_t filter)
{
  return filter == EVFILT_READ || filter == EVFILT_WRITE ||
	filter == EVFILT_VNODE;
}

static int kq_is_regular(struct filp *f)
{
  return f->filp_vno != NULL && S_ISREG(f->filp_vno->v_mode);
}

static void kq_remove(struct kqueue *kq, int i)
{
  if (kq->kq_knotes[i].kn_watch)
	kq_watchers--;
  kq->kq_knotes[i] = kq->kq_knotes[--kq->kq_nknotes];
}

/*===========================================================================*
 *				do_kqueue				     *
 *===========================================================================*/
int do_kqueue(void)
{
/* Perform the kqueue1(flags) system call. */
  struct filp *f;
  struct vnode *vp;
  struct vmnt *vmp;
  struct node_details res;
  struct kqueue *kq;
  int r, fd, flags;

  flags = job_m_in.m_lc_vfs_kqueue.flags;
  if (flags & ~(O_CLOEXEC | O_NONBLOCK | O_NOSIGPIPE))
	return(EINVAL);

  if ((kq = calloc(1, sizeof(*kq))) == NULL)
	return(ENOMEM);

  /* The descriptor needs a vnode: take an anonymous node on PFS, as pipes
   * do.  Reads and writes on it fail (see read_write()).
   */
  if ((vmp = find_vmnt(PFS_PROC_NR)) == NULL) panic("PFS gone");
  if ((r = lock_vmnt(vmp, VMNT_READ)) != OK) {
	free(kq);
	return(r);
  }
  if ((vp = get_free_vnode()) == NULL) {
	unlock_vmnt(vmp);
	free(kq);
	return(err_code);
  }
  lock_vnode(vp, VNODE_OPCL);

  if ((r = get_fd(fp, 0, R_BIT, &fd, &f)) != OK) {
	unlock_vnode(vp);
	unlock_vmnt(vmp);
	free(kq);
	return(r);
  }

  r = req_newnode(PFS_PROC_NR, fp->fp_effuid, fp->fp_effgid, I_NAMED_PIPE,
	NO_DEV, &res);
  if (r != OK) {
	unlock_filp(f);
	unlock_vnode(vp);
	unlock_vmnt(vmp);
	free(kq);
	return(r);
  }

  vp->v_fs_e = res.fs_e;
  vp->v_mapfs_e = res.fs_e;
  vp->v_inode_nr = res.inode_nr;
  vp->v_mapinode_nr = res.inode_nr;
  vp->v_mode = res.fmode;
  vp->v_fs_count = 1;
  vp->v_mapfs_count = 1;
  vp->v_ref_count = 1;
  vp->v_size = 0;
  vp->v_vmnt = NULL;
  vp->v_dev = NO_DEV;

  fp->fp_fd->fd_filp[fd] = f;
  f->filp_count = 1;
  f->filp_vno = vp;
  f->filp_flags = O_RDONLY | (flags & ~O_CLOEXEC);
  f->filp_kq = kq;
  if (flags & O_CLOEXEC)
	FD_SET(fd, &fp->fp_fd->fd_cloexec_set);

  kq->kq_fdtab = fp->fp_fd;
  kq->kq_next = kq_list;
  kq_list = kq;

  unlock_filp(f);
  unlock_vmnt(vmp);

  return(fd);
}

/*===========================================================================*
 *				kq_free					     *
 *===========================================================================*/
void kq_free(struct kqueue *kq)
{
/* The last descriptor for a kqueue has been closed. */
  struct kqueue **kqp;

  kq_abort(kq);		/* kevent calls still waiting on it */
  while (kq->kq_nknotes > 0)
	kq_remove(kq, kq->kq_nknotes - 1);
  for (kqp = &kq_list; *kqp != NULL; kqp = &(*kqp)->kq_next) {
	if (*kqp == kq) {
		*kqp = kq->kq_next;
		break;
	}
  }
  if (kq->kq_busy > 0) {
	kq->kq_dead = TRUE;	/* do_kevent() frees it */
	return;
  }
  free(kq->kq_knotes);
  free(kq);
}

/* A do_kevent() call is done with the kqueue. */
static int kq_release(struct kqueue *kq, int r)
{
  if (--kq->kq_busy == 0 && kq->kq_dead) {
	free(kq->kq_knotes);
	free(kq);
  }
  return(r);
}

/*===========================================================================*
 *				kq_fd_closed				     *
 *===========================================================================*/
void kq_fd_closed(struct filedesc *fdtab, int fd)
{
/* A descriptor of the given table has been closed: its knotes go. */
  struct kqueue *kq;
  int i;

  for (kq = kq_list; kq != NULL; kq = kq->kq_next) {
	if (kq->kq_fdtab != fdtab)
		continue;
	for (i = 0; i < kq->kq_nknotes; ) {
		if (kq_is_fdfilter(kq->kq_knotes[i].kn_filter) &&
		    kq->kq_knotes[i].kn_ident == (uintptr_t) fd)
			kq_remove(kq, i);
		else
			i++;
	}
  }
}

/*===========================================================================*
 *				kq_fdtab_gone				     *
 *===========================================================================*/
void kq_fdtab_gone(struct filedesc *fdtab)
{
/* A descriptor table is no longer in use: kqueues of it that live on (as
 * descriptors inherited elsewhere) can no longer be used.
 */
  struct kqueue *kq;
  int i;

  for (kq = kq_list; kq != NULL; kq = kq->kq_next) {
	if (kq->kq_fdtab != fdtab)
		continue;
	kq->kq_fdtab = NULL;
	for (i = 0; i < kq->kq_nknotes; ) {	/* their filps are gone */
		if (kq_is_fdfilter(kq->kq_knotes[i].kn_filter))
			kq_remove(kq, i);
		else
			i++;
	}
  }
}

/*===========================================================================*
 *				kq_vnode_write				     *
 *===========================================================================*/
void kq_vnode_write(struct vnode *vp, int extended)
{
/* The given regular file has been written to, and grown if 'extended'. */
  struct kqueue *kq;
  struct knote *kn;
  int i, wake;

  if (kq_watchers == 0)
	return;

  for (kq = kq_list; kq != NULL; kq = kq->kq_next) {
	wake = FALSE;
	for (i = 0; i < kq->kq_nknotes; i++) {
		kn = &kq->kq_knotes[i];
		if (!kn->kn_watch || kn->kn_filp->filp_vno != vp)
			continue;
		if (kn->kn_filter == EVFILT_VNODE) {
			kn->kn_pending |= kn->kn_fflags & (NOTE_WRITE |
			    (extended ? NOTE_EXTEND : 0));
			if (kn->kn_pending == 0)
				continue;
		}
		wake = TRUE;
	}
	if (wake)
		kq_wake(kq);
  }
}

/*===========================================================================*
 *				kq_find					     *
 *===========================================================================*/
static struct knote *kq_find(struct kqueue *kq, uintptr_t ident,
	uint32_t filter)
{
  int i;

  for (i = 0; i < kq->kq_nknotes; i++)
	if (kq->kq_knotes[i].kn_ident == ident &&
	    kq->kq_knotes[i].kn_filter == filter)
		return &kq->kq_knotes[i];
  return NULL;
}

/*===========================================================================*
 *				kq_ms2ticks				     *
 *===========================================================================*/
static clock_t kq_ms2ticks(int64_t ms)
{
  clock_t t;

  if (ms <= 0)
	return 1;
  if (ms / 1000 >= (TMRDIFF_MAX - 1) / system_hz)
	return TMRDIFF_MAX;
  t = (clock_t) ((ms * system_hz + 999) / 1000);
  return t > 0 ? t : 1;
}

/*===========================================================================*
 *				kq_change				     *
 *===========================================================================*/
static int kq_change(struct kqueue *kq, struct kevent *kev)
{
/* Apply one change to a kqueue.  Returns OK or an error for EV_ERROR. */
  struct knote *kn;
  struct filp *f = NULL;
  uint32_t action = kev->flags & (EV_ADD | EV_DELETE | EV_ENABLE |
	EV_DISABLE);

  switch (kev->filter) {
  case EVFILT_READ:
  case EVFILT_WRITE:
  case EVFILT_VNODE:
	if (kev->ident >= OPEN_MAX ||
	    (f = get_filp2(fp, (int) kev->ident, VNODE_NONE)) == NULL)
		return(EBADF);
	if (f->filp_kq != NULL)
		return(EINVAL);		/* a kqueue on itself or another */
	/* Of the vnode notes, only writes are known here: refuse the others
	 * rather than never report them (tail -F then polls instead).
	 */
	if (kev->filter == EVFILT_VNODE && (!kq_is_regular(f) ||
	    (kev->fflags & ~(NOTE_WRITE | NOTE_EXTEND))))
		return(EINVAL);
	break;
  case EVFILT_TIMER:
	if ((action & EV_ADD) && kev->data < 0)
		return(EINVAL);
	break;
  case EVFILT_USER:
	break;
  default:
	return(EINVAL);
  }

  kn = kq_find(kq, kev->ident, kev->filter);

  if (kn == NULL) {
	if (!(action & EV_ADD))
		return(ENOENT);

	if (kq->kq_nknotes == kq->kq_maxknotes) {
		struct knote *nk;
		int max = kq->kq_maxknotes ? kq->kq_maxknotes * 2 : 8;

		if (max > KQ_MAXKNOTES)
			return(ENOMEM);
		if ((nk = realloc(kq->kq_knotes, max * sizeof(*nk))) == NULL)
			return(ENOMEM);
		kq->kq_knotes = nk;
		kq->kq_maxknotes = max;
	}
	kn = &kq->kq_knotes[kq->kq_nknotes++];
	memset(kn, 0, sizeof(*kn));
	kn->kn_ident = kev->ident;
	kn->kn_filter = kev->filter;
	kn->kn_filp = f;
	/* Does it wait for writes to its file (see kq_vnode_write())? */
	kn->kn_watch = kn->kn_filter == EVFILT_VNODE ||
	    (kn->kn_filter == EVFILT_READ && kq_is_regular(f));
	if (kn->kn_watch)
		kq_watchers++;
  } else if (action & EV_DELETE) {
	kq_remove(kq, kn - kq->kq_knotes);
	return(OK);
  }

  if (action & EV_ADD) {
	kn->kn_flags = kev->flags & (EV_ONESHOT | EV_CLEAR | EV_DISPATCH);
	kn->kn_udata = kev->udata;
	kn->kn_disabled = FALSE;
	switch (kn->kn_filter) {
	case EVFILT_TIMER:
		kn->kn_period = kq_ms2ticks(kev->data);
		/* The current tick is partly over: one more, so as not to
		 * fire early.
		 */
		kn->kn_expire = getticks() + kn->kn_period + 1;
		if (kn->kn_flags & EV_ONESHOT)
			kn->kn_period = 0;
		break;
	case EVFILT_VNODE:
		kn->kn_fflags = kev->fflags;
		break;
	case EVFILT_USER:
		kn->kn_fflags = kev->fflags & NOTE_FFLAGSMASK;
		break;
	default:
		kn->kn_fflags = kev->fflags;
		kn->kn_data = kev->data;
	}
  }

  if (action & EV_DELETE) {
	kq_remove(kq, kn - kq->kq_knotes);
	return(OK);
  }
  if (action & EV_ENABLE)
	kn->kn_disabled = FALSE;
  if (action & EV_DISABLE)
	kn->kn_disabled = TRUE;

  if (kn->kn_filter == EVFILT_USER) {
	/* Update the user flags, and trigger. */
	switch (kev->fflags & NOTE_FFCTRLMASK) {
	case NOTE_FFAND:
		kn->kn_fflags &= kev->fflags & NOTE_FFLAGSMASK;
		break;
	case NOTE_FFOR:
		kn->kn_fflags |= kev->fflags & NOTE_FFLAGSMASK;
		break;
	case NOTE_FFCOPY:
		kn->kn_fflags = kev->fflags & NOTE_FFLAGSMASK;
		break;
	}
	if (kev->fflags & NOTE_TRIGGER) {
		kn->kn_triggered = TRUE;
		kq_wake(kq);
	}
  }

  return(OK);
}

/*===========================================================================*
 *				kq_ready				     *
 *===========================================================================*/
static int kq_ready(struct knote *kn, clock_t now, fd_set *rdready,
	fd_set *wrready, struct kevent *kev)
{
/* Is the given knote ready?  If so, fill in its event. */
  struct vnode *vp;
  int64_t data = 0;
  uint32_t flags = 0, fflags = 0;

  if (kn->kn_disabled)
	return FALSE;

  switch (kn->kn_filter) {
  case EVFILT_READ:
  case EVFILT_WRITE:
	vp = kn->kn_filp->filp_vno;
	if (kq_is_regular(kn->kn_filp)) {
		if (kn->kn_filter == EVFILT_WRITE)
			break;			/* always writable */
		if (kn->kn_filp->filp_pos >= vp->v_size)
			return FALSE;
		data = vp->v_size - kn->kn_filp->filp_pos;
		break;
	}
	if (rdready == NULL || !FD_ISSET((int) kn->kn_ident,
	    kn->kn_filter == EVFILT_READ ? rdready : wrready))
		return FALSE;
	if (vp != NULL && S_ISFIFO(vp->v_mode)) {
		/* A pipe: the bytes in it, or the room left; and whether the
		 * other end is gone.
		 */
		if (kn->kn_filter == EVFILT_READ) {
			data = vp->v_size;
			if (find_filp(vp, W_BIT) == NULL)
				flags |= EV_EOF;
		} else {
			data = PIPE_BUF > vp->v_size ? PIPE_BUF - vp->v_size :
			    0;
			if (find_filp(vp, R_BIT) == NULL)
				flags |= EV_EOF;
		}
	}
	break;
  case EVFILT_VNODE:
	if (kn->kn_pending == 0)
		return FALSE;
	fflags = kn->kn_pending;
	break;
  case EVFILT_TIMER:
	if (kq_before(now, kn->kn_expire))
		return FALSE;
	data = 1;
	if (kn->kn_period > 0)
		data += (now - kn->kn_expire) / kn->kn_period;
	break;
  case EVFILT_USER:
	if (!kn->kn_triggered)
		return FALSE;
	fflags = kn->kn_fflags;
	break;
  default:
	return FALSE;
  }

  kev->ident = kn->kn_ident;
  kev->filter = kn->kn_filter;
  kev->flags = kn->kn_flags | flags;
  kev->fflags = fflags;
  kev->data = data;
  kev->udata = kn->kn_udata;
  return TRUE;
}

/*===========================================================================*
 *				kq_reported				     *
 *===========================================================================*/
static int kq_reported(struct kqueue *kq, int i, int64_t count)
{
/* The knote has just been reported.  Returns whether it is still there. */
  struct knote *kn = &kq->kq_knotes[i];

  switch (kn->kn_filter) {
  case EVFILT_TIMER:
	if (kn->kn_period > 0)
		kn->kn_expire += count * kn->kn_period;
	break;
  case EVFILT_VNODE:
	kn->kn_pending = 0;
	break;
  case EVFILT_USER:
	if (kn->kn_flags & EV_CLEAR)
		kn->kn_triggered = FALSE;
	break;
  }

  if ((kn->kn_flags & EV_ONESHOT) ||
      (kn->kn_filter == EVFILT_TIMER && kn->kn_period == 0)) {
	kq_remove(kq, i);
	return FALSE;
  }
  if (kn->kn_flags & EV_DISPATCH)
	kn->kn_disabled = TRUE;
  return TRUE;
}

/*===========================================================================*
 *				kq_output				     *
 *===========================================================================*/
int kq_output(struct kqueue *kq, endpoint_t ep, fd_set *rdready,
	fd_set *wrready, vir_bytes events, int nevents)
{
/* Collect the ready events of a kqueue into the caller's event list, up to
 * 'nevents' of them, with the descriptors that select found ready (if any).
 * Returns their number, or an error.  This function MUST NOT block its
 * calling thread.
 */
  static struct kevent buf[KQ_CHUNK];
  clock_t now;
  int i, n, done, r;

  now = getticks();
  done = n = 0;
  for (i = 0; i < kq->kq_nknotes && done + n < nevents; ) {
	if (!kq_ready(&kq->kq_knotes[i], now, rdready, wrready, &buf[n])) {
		i++;
		continue;
	}
	if (kq_reported(kq, i, buf[n].data))
		i++;
	if (++n == KQ_CHUNK) {
		if ((r = sys_datacopy_wrapper(SELF, (vir_bytes) buf, ep,
		    events + done * sizeof(buf[0]), n * sizeof(buf[0]))) != OK)
			return(r);
		done += n;
		n = 0;
	}
  }
  if (n > 0 && (r = sys_datacopy_wrapper(SELF, (vir_bytes) buf, ep,
      events + done * sizeof(buf[0]), n * sizeof(buf[0]))) != OK)
	return(r);
  return(done + n);
}

/*===========================================================================*
 *				do_kevent				     *
 *===========================================================================*/
int do_kevent(void)
{
/* Perform the kevent(kq, changelist, nchanges, eventlist, nevents, timeout)
 * system call.
 */
  struct kevent buf[KQ_CHUNK];
  struct timespec ts;
  struct filp *f;
  struct kqueue *kq;
  struct knote *kn;
  fd_set rd, wr;
  vir_bytes changes, events, vtimeout;
  int r, i, n, fd, nchanges, nevents, nerrors, nfds, block, pending;
  clock_t now, ticks, due;

  fd = job_m_in.m_lc_vfs_kevent.fd;
  changes = job_m_in.m_lc_vfs_kevent.changelist;
  nchanges = job_m_in.m_lc_vfs_kevent.nchanges;
  events = job_m_in.m_lc_vfs_kevent.eventlist;
  nevents = job_m_in.m_lc_vfs_kevent.nevents;
  vtimeout = job_m_in.m_lc_vfs_kevent.timeout;

  if ((f = get_filp(fd, VNODE_NONE)) == NULL)
	return(err_code);
  if ((kq = f->filp_kq) == NULL || kq->kq_fdtab != fp->fp_fd)
	return(EBADF);
  if (nchanges < 0 || nevents < 0)
	return(EINVAL);

  /* Copying from and to the caller may block (on a page fault), and another
   * thread may close the kqueue meanwhile: keep it until done with it.
   */
  kq->kq_busy++;

  /* Get the timeout, if any. */
  if (vtimeout != 0) {
	if ((r = sys_datacopy_wrapper(who_e, vtimeout, SELF, (vir_bytes) &ts,
	    sizeof(ts))) != OK)
		return kq_release(kq, r);
	if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000L)
		return kq_release(kq, EINVAL);
  }

  /* Apply the changes.  Errors become EV_ERROR events while there is room
   * for them (and so do all changes with EV_RECEIPT); a call with any of
   * those returns at once.
   */
  nerrors = 0;
  for (i = 0; i < nchanges; i += n) {
	int j;

	n = MIN(nchanges - i, KQ_CHUNK);
	if ((r = sys_datacopy_wrapper(who_e, changes + i * sizeof(buf[0]),
	    SELF, (vir_bytes) buf, n * sizeof(buf[0]))) != OK)
		return kq_release(kq, r);
	for (j = 0; j < n; j++) {
		if (kq->kq_dead)
			return kq_release(kq, EBADF);
		r = kq_change(kq, &buf[j]);
		if (r == OK && !(buf[j].flags & EV_RECEIPT))
			continue;
		if (nerrors >= nevents) {
			if (r != OK)
				return kq_release(kq, r);
			continue;
		}
		buf[j].flags = EV_ERROR;
		buf[j].data = -r;	/* a user errno: VFS's are negative */
		if ((r = sys_datacopy_wrapper(SELF, (vir_bytes) &buf[j],
		    who_e, events + nerrors * sizeof(buf[j]),
		    sizeof(buf[j]))) != OK)
			return kq_release(kq, r);
		nerrors++;
	}
  }
  if (kq->kq_dead)
	return kq_release(kq, EBADF);
  if (nerrors > 0 || nevents == 0)
	return kq_release(kq, nerrors);

  /* Wait for the events.  Descriptors other than regular files go to the
   * select machinery; anything else that is ready right away makes the call
   * not wait; a pending timer limits the wait.
   */
  FD_ZERO(&rd);
  FD_ZERO(&wr);
  nfds = 0;
  pending = FALSE;
  now = getticks();
  due = 0;
  for (i = 0; i < kq->kq_nknotes; i++) {
	struct kevent dummy;

	kn = &kq->kq_knotes[i];
	if (kn->kn_disabled)
		continue;
	if ((kn->kn_filter == EVFILT_READ || kn->kn_filter == EVFILT_WRITE) &&
	    !kq_is_regular(kn->kn_filp)) {
		FD_SET((int) kn->kn_ident, kn->kn_filter == EVFILT_READ ?
		    &rd : &wr);
		if ((int) kn->kn_ident >= nfds)
			nfds = (int) kn->kn_ident + 1;
		continue;
	}
	if (kq_ready(kn, now, NULL, NULL, &dummy))
		pending = TRUE;
	else if (kn->kn_filter == EVFILT_TIMER &&
	    (due == 0 || kn->kn_expire - now < due))
		due = kn->kn_expire - now;	/* not due yet: positive */
  }

  block = !pending;
  ticks = 0;
  if (vtimeout != 0) {
	if (ts.tv_sec == 0 && ts.tv_nsec == 0)
		block = FALSE;
	else if (ts.tv_sec >= (TMRDIFF_MAX - 1) / system_hz)
		ticks = TMRDIFF_MAX;
	else
		ticks = ts.tv_sec * system_hz +
		    (ts.tv_nsec / 1000 * system_hz + 999999) / 1000000;
	if (ticks == 0 && block)
		ticks = 1;
  }
  if (due > 0 && (ticks == 0 || due < ticks))
	ticks = due;

  if (nfds == 0 && !block)
	return kq_release(kq, kq_output(kq, who_e, NULL, NULL, events,
	    nevents));

  /* From here on, a kqueue closed meanwhile is kq_abort()'s business. */
  kq_release(kq, OK);
  return select_kevent(kq, &rd, &wr, nfds, block, ticks, events, nevents);
}
