/*	MINIX: stand-in for the config.h that wayland's meson build generates.
 *
 * Each answer below was checked against the tree rather than assumed; the
 * notes say where.
 */

#ifndef MINIX_WL_CONFIG_H
#define MINIX_WL_CONFIG_H

/* strndup(3): declared in <string.h>. */
#define HAVE_STRNDUP 1

/*
 * accept4(2): absent from MINIX's <sys/socket.h>.  wayland-os.c falls back to
 * accept(2) followed by an explicit FD_CLOEXEC, which is what we want.
 */
/* #undef HAVE_ACCEPT4 */

/*
 * MSG_CMSG_CLOEXEC is defined by <sys/socket.h> and MINIX's UDS honours it, so
 * the "broken" workaround (re-walking the cmsg list to set FD_CLOEXEC by hand)
 * is not needed.
 */
/* #undef HAVE_BROKEN_MSG_CMSG_CLOEXEC */

/*
 * memfd_create(2): absent.  wayland-shm.c only uses it to seal its own
 * anonymous pools; without it, it falls back to a temporary file, and clients
 * on MINIX supply their pool fds via shm_open(3) anyway (see wlprobe).
 */
/* #undef HAVE_MEMFD_CREATE */

/*
 * <sys/ucred.h> exists, but it is NetBSD's, not FreeBSD's: there is no
 * struct xucred and no LOCAL_PEERCRED.  Leaving this undefined keeps
 * wayland-os.c off the FreeBSD path and on the __minix branch added there,
 * which uses LOCAL_PEEREID.
 */
/* #undef HAVE_SYS_UCRED_H */
/* #undef HAVE_XUCRED_CR_PID */

/* libxml2 is only used by wayland-scanner to validate against wayland.dtd;
 * validation is optional and we do not build it. */
/* #undef HAVE_LIBXML */

#endif /* MINIX_WL_CONFIG_H */
