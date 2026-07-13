/*	MINIX: epoll/timerfd/signalfd/eventfd emulation over poll(2).
 *
 * Rationale, and the one capability this cannot provide, are documented in
 * include/sys/epoll.h.  In short: MINIX has neither epoll nor kqueue, so
 * libwayland's event loop is given the Linux interfaces it expects, built on
 * poll(2), and src/event-loop.c compiles unmodified.
 *
 * The descriptors handed out here are real file descriptors (pipe ends), so
 * they can be registered, closed and counted like any other -- but their
 * readiness is decided by this file, not by the kernel:
 *
 *   epoll set   a descriptor used purely as a handle; the watch list lives in
 *               the table below and epoll_wait() does the work with poll(2).
 *   timerfd     a pipe that is never written.  libwayland never read(2)s it
 *               (the timer source's dispatch is noop_dispatch); it only waits
 *               for readability and then drains its own timer heap.  So
 *               epoll_wait() derives its poll(2) timeout from the earliest
 *               armed deadline and synthesises EPOLLIN when one comes due.
 *   signalfd    a self-pipe fed by a signal handler; libwayland *does* read(2)
 *               this one, so the handler writes a whole signalfd_siginfo.
 *   eventfd     a pipe; the uint64_t counter protocol libwayland uses (write 1,
 *               read it back) maps onto it byte-for-byte.
 *
 * Table sizes are static: a compositor creates one event loop, one timerfd and
 * a handful of signal sources, so a linear scan over a few entries is cheaper
 * than the allocator.  Running out is a hard error rather than silent
 * misbehaviour.
 */

#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>

/* This file implements the redirects, so it must not be subject to them. */
#undef close
#undef write
#undef sigprocmask

#include <sys/poll.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_EPOLL	8
#define MAX_WATCH	64
#define MAX_TIMER	16
#define MAX_SIGNAL	16
#define MAX_EVENT	16

struct watch {
	int		used;
	int		fd;
	uint32_t	events;
	epoll_data_t	data;
};

struct epoll_inst {
	int		used;
	int		fd;		/* handle (pipe read end) */
	int		wfd;		/* its write end, kept only to close */
	struct watch	w[MAX_WATCH];
};

struct timer {
	int		used;
	int		fd;		/* pipe read end, never written */
	int		wfd;
	int		armed;
	struct timespec	deadline;	/* absolute, CLOCK_MONOTONIC */
};

struct sigpipe {
	int		used;
	int		fd;		/* read end */
	int		wfd;		/* write end, used by the handler */
};

struct evpipe {
	int		used;
	int		fd;		/* read end */
	int		wfd;		/* write end, target of write(2) */
};

static struct epoll_inst epolls[MAX_EPOLL];
static struct timer timers[MAX_TIMER];
static struct sigpipe sigpipes[MAX_SIGNAL];
static struct evpipe evpipes[MAX_EVENT];

/* signo -> write end, for the signal handler.  Written before the handler is
 * installed, read only from handler context. */
static volatile sig_atomic_t sig_wfd[NSIG];

/*
 * Create a pipe and apply the CLOEXEC/NONBLOCK intent of the Linux *_CLOEXEC /
 * *_NONBLOCK flags.  NONBLOCK is applied to the read end, which is the end
 * libwayland holds and reads.
 */
static int
make_pipe(int *rfd, int *wfd, int nonblock)
{
	int p[2];

	if (pipe(p) != 0)
		return -1;

	(void)fcntl(p[0], F_SETFD, FD_CLOEXEC);
	(void)fcntl(p[1], F_SETFD, FD_CLOEXEC);

	if (nonblock) {
		(void)fcntl(p[0], F_SETFL,
		    fcntl(p[0], F_GETFL, 0) | O_NONBLOCK);
		(void)fcntl(p[1], F_SETFL,
		    fcntl(p[1], F_GETFL, 0) | O_NONBLOCK);
	}

	*rfd = p[0];
	*wfd = p[1];
	return 0;
}

static struct epoll_inst *
find_epoll(int fd)
{
	int i;

	for (i = 0; i < MAX_EPOLL; i++)
		if (epolls[i].used && epolls[i].fd == fd)
			return &epolls[i];
	return NULL;
}

static struct timer *
find_timer(int fd)
{
	int i;

	for (i = 0; i < MAX_TIMER; i++)
		if (timers[i].used && timers[i].fd == fd)
			return &timers[i];
	return NULL;
}

/*===========================================================================*
 *				epoll					     *
 *===========================================================================*/
int
epoll_create1(int flags __attribute__((unused)))
{
	int i, rfd, wfd;

	for (i = 0; i < MAX_EPOLL; i++)
		if (!epolls[i].used)
			break;
	if (i == MAX_EPOLL) {
		errno = EMFILE;
		return -1;
	}

	/* The handle is never polled or read; it exists so the caller has a
	 * genuine fd to hold, pass around and close. */
	if (make_pipe(&rfd, &wfd, 1) != 0)
		return -1;

	memset(&epolls[i], 0, sizeof(epolls[i]));
	epolls[i].used = 1;
	epolls[i].fd = rfd;
	epolls[i].wfd = wfd;
	return rfd;
}

int
epoll_create(int size __attribute__((unused)))
{
	return epoll_create1(0);
}

int
epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
	struct epoll_inst *ep;
	struct watch *free_slot = NULL;
	int i;

	if ((ep = find_epoll(epfd)) == NULL) {
		errno = EBADF;
		return -1;
	}

	for (i = 0; i < MAX_WATCH; i++) {
		if (ep->w[i].used && ep->w[i].fd == fd) {
			switch (op) {
			case EPOLL_CTL_ADD:
				errno = EEXIST;
				return -1;
			case EPOLL_CTL_MOD:
				if (event == NULL) {
					errno = EFAULT;
					return -1;
				}
				ep->w[i].events = event->events;
				ep->w[i].data = event->data;
				return 0;
			case EPOLL_CTL_DEL:
				ep->w[i].used = 0;
				return 0;
			default:
				errno = EINVAL;
				return -1;
			}
		}
		if (!ep->w[i].used && free_slot == NULL)
			free_slot = &ep->w[i];
	}

	if (op != EPOLL_CTL_ADD) {
		errno = ENOENT;		/* MOD/DEL of an fd we never saw */
		return -1;
	}
	if (event == NULL) {
		errno = EFAULT;
		return -1;
	}
	if (free_slot == NULL) {
		errno = ENOSPC;
		return -1;
	}

	free_slot->used = 1;
	free_slot->fd = fd;
	free_slot->events = event->events;
	free_slot->data = event->data;
	return 0;
}

/* now - then, in milliseconds, clamped at 0. */
static int
ms_until(const struct timespec *deadline, const struct timespec *now)
{
	long long ms;

	ms = (long long)(deadline->tv_sec - now->tv_sec) * 1000;
	ms += (deadline->tv_nsec - now->tv_nsec) / 1000000;
	if (ms < 0)
		return 0;
	if (ms > 3600000)		/* an hour is as long as we need to wait */
		return 3600000;
	return (int)ms;
}

int
epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
	struct epoll_inst *ep;
	struct pollfd pfd[MAX_WATCH];
	struct watch *wp[MAX_WATCH];
	struct timespec now;
	int i, n = 0, nready = 0, rc;
	int deadline_ms = -1;

	if ((ep = find_epoll(epfd)) == NULL) {
		errno = EBADF;
		return -1;
	}
	if (maxevents <= 0) {
		errno = EINVAL;
		return -1;
	}

	clock_gettime(CLOCK_MONOTONIC, &now);

	/*
	 * Build the poll set.  Timerfds are held back: they are never readable
	 * in the kernel's eyes, so their readiness is computed below from the
	 * armed deadline instead.  An armed timer also caps how long we may
	 * block.
	 */
	for (i = 0; i < MAX_WATCH; i++) {
		struct timer *t;

		if (!ep->w[i].used)
			continue;

		if ((t = find_timer(ep->w[i].fd)) != NULL) {
			if (t->armed) {
				int ms = ms_until(&t->deadline, &now);

				if (deadline_ms < 0 || ms < deadline_ms)
					deadline_ms = ms;
			}
			continue;
		}

		pfd[n].fd = ep->w[i].fd;
		pfd[n].events = 0;
		pfd[n].revents = 0;
		if (ep->w[i].events & EPOLLIN)
			pfd[n].events |= POLLIN;
		if (ep->w[i].events & EPOLLOUT)
			pfd[n].events |= POLLOUT;
		wp[n] = &ep->w[i];
		n++;
	}

	if (deadline_ms >= 0 && (timeout < 0 || deadline_ms < timeout))
		timeout = deadline_ms;

	rc = poll(pfd, (nfds_t)n, timeout);
	if (rc < 0)
		return -1;		/* EINTR included: caller retries */

	for (i = 0; i < n && nready < maxevents; i++) {
		uint32_t e = 0;

		if (pfd[i].revents == 0)
			continue;
		if (pfd[i].revents & POLLIN)
			e |= EPOLLIN;
		if (pfd[i].revents & POLLOUT)
			e |= EPOLLOUT;
		if (pfd[i].revents & POLLERR)
			e |= EPOLLERR;
		if (pfd[i].revents & POLLHUP)
			e |= EPOLLHUP;
		if (pfd[i].revents & POLLNVAL)
			e |= EPOLLERR;
		if (e == 0)
			continue;

		events[nready].events = e;
		events[nready].data = wp[i]->data;
		nready++;
	}

	/* Re-read the clock: poll(2) may have slept right up to a deadline. */
	clock_gettime(CLOCK_MONOTONIC, &now);

	for (i = 0; i < MAX_WATCH && nready < maxevents; i++) {
		struct timer *t;

		if (!ep->w[i].used)
			continue;
		if ((t = find_timer(ep->w[i].fd)) == NULL || !t->armed)
			continue;
		if (ms_until(&t->deadline, &now) > 0)
			continue;	/* not due yet */

		/*
		 * Due.  Report readable, exactly as a real timerfd would.  We
		 * do NOT disarm: libwayland re-arms (or clears) the timerfd
		 * from its heap after dispatching, and a real timerfd stays
		 * readable until then too.
		 */
		events[nready].events = EPOLLIN;
		events[nready].data = ep->w[i].data;
		nready++;
	}

	return nready;
}

/*===========================================================================*
 *				timerfd					     *
 *===========================================================================*/
int
timerfd_create(int clockid __attribute__((unused)), int flags)
{
	int i, rfd, wfd;

	for (i = 0; i < MAX_TIMER; i++)
		if (!timers[i].used)
			break;
	if (i == MAX_TIMER) {
		errno = EMFILE;
		return -1;
	}

	if (make_pipe(&rfd, &wfd, (flags & TFD_NONBLOCK) != 0) != 0)
		return -1;

	timers[i].used = 1;
	timers[i].fd = rfd;
	timers[i].wfd = wfd;
	timers[i].armed = 0;
	return rfd;
}

int
timerfd_settime(int fd, int flags, const struct itimerspec *new_value,
    struct itimerspec *old_value)
{
	struct timer *t;

	if ((t = find_timer(fd)) == NULL) {
		errno = EBADF;
		return -1;
	}
	if (new_value == NULL) {
		errno = EFAULT;
		return -1;
	}

	if (old_value != NULL)
		memset(old_value, 0, sizeof(*old_value));

	/* it_value == 0 disarms, per timerfd_settime(2). */
	if (new_value->it_value.tv_sec == 0 &&
	    new_value->it_value.tv_nsec == 0) {
		t->armed = 0;
		return 0;
	}

	if (flags & TFD_TIMER_ABSTIME) {
		t->deadline = new_value->it_value;
	} else {
		struct timespec now;

		clock_gettime(CLOCK_MONOTONIC, &now);
		t->deadline.tv_sec = now.tv_sec + new_value->it_value.tv_sec;
		t->deadline.tv_nsec = now.tv_nsec + new_value->it_value.tv_nsec;
		if (t->deadline.tv_nsec >= 1000000000L) {
			t->deadline.tv_nsec -= 1000000000L;
			t->deadline.tv_sec++;
		}
	}
	t->armed = 1;

	/* libwayland only ever sets a one-shot deadline (it_interval is always
	 * zero); a periodic timer would need re-arming here. */
	return 0;
}

/*===========================================================================*
 *				signalfd				     *
 *===========================================================================*/
static void
wl_sig_handler(int signo)
{
	struct signalfd_siginfo si;
	int fd, saved_errno;

	if (signo < 0 || signo >= NSIG)
		return;
	if ((fd = (int)sig_wfd[signo]) < 0)
		return;

	saved_errno = errno;		/* a handler must not clobber errno */

	memset(&si, 0, sizeof(si));
	si.ssi_signo = (uint32_t)signo;

	/* write(2) is async-signal-safe.  If the pipe is full the event is
	 * already pending, so dropping this one loses nothing. */
	(void)write(fd, &si, sizeof(si));

	errno = saved_errno;
}

int
signalfd(int fd, const sigset_t *mask, int flags)
{
	struct sigaction sa;
	sigset_t unblock;
	int i, signo, rfd, wfd;

	if (fd != -1) {
		/* libwayland only ever creates (fd == -1). */
		errno = EINVAL;
		return -1;
	}
	if (mask == NULL) {
		errno = EFAULT;
		return -1;
	}

	for (i = 0; i < MAX_SIGNAL; i++)
		if (!sigpipes[i].used)
			break;
	if (i == MAX_SIGNAL) {
		errno = EMFILE;
		return -1;
	}

	if (make_pipe(&rfd, &wfd, (flags & SFD_NONBLOCK) != 0) != 0)
		return -1;

	sigemptyset(&unblock);

	for (signo = 1; signo < NSIG; signo++) {
		if (!sigismember(mask, signo))
			continue;

		sig_wfd[signo] = wfd;

		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = wl_sig_handler;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_RESTART;
		if (sigaction(signo, &sa, NULL) != 0) {
			int e = errno;

			close(rfd);
			close(wfd);
			errno = e;
			return -1;
		}

		sigaddset(&unblock, signo);
	}

	/*
	 * Make sure the signal is deliverable now.  wl_event_loop_add_signal()
	 * will call sigprocmask(SIG_BLOCK) on it immediately *after* we return
	 * -- correct for a real signalfd, fatal for a handler-fed pipe -- but
	 * wl_minix_sigprocmask() below filters that block out.  This unblock
	 * covers the case where the signal was already blocked before we were
	 * ever called.
	 */
	(void)sigprocmask(SIG_UNBLOCK, &unblock, NULL);

	sigpipes[i].used = 1;
	sigpipes[i].fd = rfd;
	sigpipes[i].wfd = wfd;
	return rfd;
}

/*
 * Refuse to block signals we are delivering by handler for libwayland; see the
 * comment on the redirect in <sys/signalfd.h>.  Everything else is passed
 * straight through to the real sigprocmask(2).
 */
int
wl_minix_sigprocmask(int how, const sigset_t *set, sigset_t *oset)
{
	sigset_t filtered;
	int signo, dropped = 0;

	if (how != SIG_BLOCK || set == NULL)
		return sigprocmask(how, set, oset);

	filtered = *set;
	for (signo = 1; signo < NSIG; signo++) {
		if (sigismember(&filtered, signo) && sig_wfd[signo] >= 0) {
			sigdelset(&filtered, signo);
			dropped++;
		}
	}

	return sigprocmask(how, dropped ? &filtered : set, oset);
}

/*===========================================================================*
 *				eventfd					     *
 *===========================================================================*/
int
eventfd(unsigned int initval, int flags)
{
	int i, rfd, wfd;

	for (i = 0; i < MAX_EVENT; i++)
		if (!evpipes[i].used)
			break;
	if (i == MAX_EVENT) {
		errno = EMFILE;
		return -1;
	}

	if (make_pipe(&rfd, &wfd, (flags & EFD_NONBLOCK) != 0) != 0)
		return -1;

	evpipes[i].used = 1;
	evpipes[i].fd = rfd;
	evpipes[i].wfd = wfd;

	/* libwayland always passes 0; a non-zero counter would have to be
	 * pre-loaded into the pipe to be readable. */
	if (initval != 0) {
		uint64_t v = initval;

		(void)write(wfd, &v, sizeof(v));
	}

	return rfd;
}

ssize_t
wl_minix_write(int fd, const void *buf, size_t count)
{
	int i;

	/* An eventfd is one descriptor to its user but a pipe to us: send the
	 * write to the far end. */
	for (i = 0; i < MAX_EVENT; i++)
		if (evpipes[i].used && evpipes[i].fd == fd)
			return write(evpipes[i].wfd, buf, count);

	return write(fd, buf, count);
}

/*===========================================================================*
 *				close					     *
 *===========================================================================*/
int
wl_minix_close(int fd)
{
	int i;

	for (i = 0; i < MAX_EPOLL; i++) {
		if (epolls[i].used && epolls[i].fd == fd) {
			epolls[i].used = 0;
			close(epolls[i].wfd);
			return close(fd);
		}
	}
	for (i = 0; i < MAX_TIMER; i++) {
		if (timers[i].used && timers[i].fd == fd) {
			timers[i].used = 0;
			close(timers[i].wfd);
			return close(fd);
		}
	}
	for (i = 0; i < MAX_SIGNAL; i++) {
		if (sigpipes[i].used && sigpipes[i].fd == fd) {
			int signo;

			/* Stop the handler writing into a descriptor we are
			 * about to close. */
			for (signo = 1; signo < NSIG; signo++)
				if (sig_wfd[signo] == sigpipes[i].wfd) {
					(void)signal(signo, SIG_DFL);
					sig_wfd[signo] = -1;
				}

			sigpipes[i].used = 0;
			close(sigpipes[i].wfd);
			return close(fd);
		}
	}
	for (i = 0; i < MAX_EVENT; i++) {
		if (evpipes[i].used && evpipes[i].fd == fd) {
			evpipes[i].used = 0;
			close(evpipes[i].wfd);
			return close(fd);
		}
	}

	return close(fd);
}

/* sig_wfd[] must start as "no pipe" rather than fd 0. */
__attribute__((constructor)) static void
wl_minix_compat_init(void)
{
	int i;

	for (i = 0; i < NSIG; i++)
		sig_wfd[i] = -1;
}
