/* netbsd */
#ifndef	SYSCONFDIR
#define	SYSCONFDIR		"/etc"
#define	SBINDIR			"/sbin"
#define	LIBDIR			"/lib"
#define	LIBEXECDIR		"/libexec"
#define	DBDIR			"/var/db/dhcpcd"
#define	RUNDIR			"/var/run/dhcpcd"
#endif
#ifndef PRIVSEP_USER
#define PRIVSEP_USER		 "_dhcpcd"
#endif
#if !defined(__minix)
#define	HAVE_IFAM_PID
#define	HAVE_IFAM_ADDRFLAGS
#define	HAVE_IFADDRS_ADDRFLAGS
#endif /* !defined(__minix) */
/* MINIX: struct ifa_msghdr has no ifam_pid/ifam_addrflags, and struct ifaddrs
 * has no ifa_addrflags. */
#define	HAVE_OPEN_MEMSTREAM
#define	HAVE_UTIL_H
#define	HAVE_SYS_QUEUE_H
#define	HAVE_SYS_RBTREE_H
#define	HAVE_REALLOCARRAY
#if !defined(__minix)
#define	HAVE_PPOLL
#else
/* MINIX has no ppoll(2), but libc now provides pollts(2) and pselect(2) as
 * thin poll/select + sigprocmask wrappers, so use the native pollts(2) path
 * (eloop.c maps ppoll -> pollts) instead of the old eloop_ppoll() shim. */
#define	HAVE_POLLTS
#endif /* !defined(__minix) */
#define	HAVE_SYS_BITOPS_H
#define	HAVE_MD5_H
#define	SHA2_H			<sha2.h>
