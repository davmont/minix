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
#endif /* !defined(__minix) */
/* MINIX implements neither ppoll(2), pollts(2) nor pselect(2) -- only poll(2)
 * and select(2).  Defining none of HAVE_PPOLL/HAVE_POLLTS/HAVE_PSELECT makes
 * eloop.c fall back to its eloop_ppoll() wrapper, which we patch on MINIX to
 * use select(2) + sigprocmask(2). */
#define	HAVE_SYS_BITOPS_H
#define	HAVE_MD5_H
#define	SHA2_H			<sha2.h>
