/*	MINIX: private epoll(7) emulation for libwayland.
 *
 * libwayland's event loop (src/event-loop.c) is written against Linux's
 * epoll/timerfd/signalfd/eventfd.  MINIX has none of these -- and, unlike the
 * BSDs, it has no kqueue either (kevent/kqueue are both listed in libc's
 * MISSING_SYSCALLS), so the usual epoll-shim-over-kqueue route is closed.
 * All MINIX offers is poll(2).
 *
 * Rather than fork event-loop.c -- the most intricate file in libwayland, and
 * the one we would then have to re-merge on every update -- these headers
 * emulate just the slice of the Linux API that libwayland actually uses, on
 * top of poll(2).  event-loop.c is then compiled unmodified.
 *
 * This works only because of one fact, verified against the source: nothing
 * inside libwayland ever *polls* the epoll fd itself.  wl_event_loop_get_fd()
 * exposes it so an application can nest the loop inside another (GLib, say),
 * but libwayland never does so internally.  A userspace shim cannot make its
 * epoll handle become readable when a watched fd is ready, so that -- and only
 * that -- is the capability we give up.  A compositor that drives the loop with
 * wl_event_loop_dispatch() is unaffected.
 *
 * These headers are DELIBERATELY PRIVATE to the libwayland build (see
 * ../../lib/libwayland-server/Makefile) and are not installed into
 * /usr/include.  A system-wide <sys/epoll.h> whose fd is not pollable would be
 * a trap for the next port that came along.
 */

#ifndef MINIX_WL_SYS_EPOLL_H
#define MINIX_WL_SYS_EPOLL_H

/* <unistd.h> MUST come before the close(2) redirect below: if it were included
 * afterwards, its own declaration of close() would be eaten by the macro. */
#include <unistd.h>

#include <stdint.h>

/* Values match Linux; nothing outside this shim depends on that, but it keeps
 * the emulation easy to compare against the real thing. */
#define EPOLLIN		0x001
#define EPOLLOUT	0x004
#define EPOLLERR	0x008
#define EPOLLHUP	0x010

#define EPOLL_CLOEXEC	0x80000

#define EPOLL_CTL_ADD	1
#define EPOLL_CTL_DEL	2
#define EPOLL_CTL_MOD	3

typedef union epoll_data {
	void		*ptr;
	int		 fd;
	uint32_t	 u32;
	uint64_t	 u64;
} epoll_data_t;

struct epoll_event {
	uint32_t	 events;
	epoll_data_t	 data;
};

int epoll_create1(int flags);
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
int epoll_wait(int epfd, struct epoll_event *events, int maxevents,
    int timeout);

/* Legacy entry point; wl_os_epoll_create_cloexec() falls back to it when
 * epoll_create1(EPOLL_CLOEXEC) reports EINVAL.  The size hint is ignored, as
 * it is on Linux. */
int epoll_create(int size);

/*
 * The shim keeps bookkeeping for the descriptors it hands out (epoll sets,
 * timer deadlines, signal pipes), so it has to learn when one is closed.
 * There is no way to hook close(2), so the libwayland build redirects it here;
 * for any fd the shim does not know about this is a plain close(2).
 */
int wl_minix_close(int fd);
#define close(fd) wl_minix_close(fd)

#endif /* MINIX_WL_SYS_EPOLL_H */
