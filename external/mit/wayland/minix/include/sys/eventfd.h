/*	MINIX: private eventfd emulation for libwayland.  See sys/epoll.h. */

#ifndef MINIX_WL_SYS_EVENTFD_H
#define MINIX_WL_SYS_EVENTFD_H

#include <sys/epoll.h>		/* for the close(2) redirect */
#include <unistd.h>		/* before the write(2) redirect, as above */

#define EFD_CLOEXEC	0x400000
#define EFD_NONBLOCK	0x004000

/*
 * libwayland uses exactly one eventfd: wl_display_terminate() write(2)s a
 * uint64_t of 1 to wake the loop, and handle_display_terminate() read(2)s it
 * back.  A pipe models that byte-for-byte, so eventfd() here returns the read
 * end of a pipe and write(2)s to it are routed to the write end by the shim's
 * bookkeeping (see wl_minix_write()).
 */
int eventfd(unsigned int initval, int flags);

/*
 * write(2) on an eventfd has to reach the pipe's *write* end, but libwayland
 * holds only the one descriptor eventfd() returned.  The build redirects
 * write(2) here so the shim can forward it; every other fd is passed straight
 * through.
 */
ssize_t wl_minix_write(int fd, const void *buf, size_t count);
#define write(fd, buf, count) wl_minix_write((fd), (buf), (count))

#endif /* MINIX_WL_SYS_EVENTFD_H */
