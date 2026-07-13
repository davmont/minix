/*	MINIX: private timerfd emulation for libwayland.  See sys/epoll.h. */

#ifndef MINIX_WL_SYS_TIMERFD_H
#define MINIX_WL_SYS_TIMERFD_H

#include <sys/epoll.h>		/* for the close(2) redirect */
#include <time.h>

#define TFD_CLOEXEC		0x400000
#define TFD_NONBLOCK		0x004000

#define TFD_TIMER_ABSTIME	(1 << 0)

/*
 * A timerfd here is a descriptor that never carries data: libwayland only ever
 * waits for it to become readable and then drains its own timer heap (the
 * dispatch for the timer source is noop_dispatch -- it never read(2)s the fd).
 * Readiness is therefore entirely a function of timerfd_settime(), which is
 * exactly what the shim tracks: epoll_wait() computes its poll(2) timeout from
 * the earliest armed deadline and synthesises EPOLLIN when one comes due.
 */
int timerfd_create(int clockid, int flags);
int timerfd_settime(int fd, int flags, const struct itimerspec *new_value,
    struct itimerspec *old_value);

#endif /* MINIX_WL_SYS_TIMERFD_H */
