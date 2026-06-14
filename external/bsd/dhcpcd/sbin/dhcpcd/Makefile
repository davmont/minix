# MINIX: dhcpcd 9.4.1, built without privilege separation (PRIVSEP).
# MINIX lacks the _dhcpcd account and the privsep machinery dhcpcd 9.x uses on
# NetBSD.  Upstream guards all of it behind #ifdef PRIVSEP, so simply not
# defining PRIVSEP and not building the privsep*.c sources yields a working
# single-process dhcpcd.  INET/INET6/AUTH/ARP/IPV4LL stay enabled.

PROG=		dhcpcd
SRCS=		common.c control.c dhcpcd.c duid.c eloop.c logerr.c
SRCS+=		if.c if-options.c route.c sa.c script.c
SRCS+=		dhcp-common.c dhcpcd-embedded.c
SRCS+=		if-bsd.c

WARNS?=		6
USE_FORT?=	yes	# network client (local server)

CPPFLAGS+=	-DHAVE_CONFIG_H

.include <bsd.own.mk>
.include "../../Makefile.inc"

.if defined(__MINIX)
CWARNFLAGS.gcc+=	-Wno-maybe-uninitialized
# reallocarray(3) lives behind _OPENBSD_SOURCE in MINIX's <stdlib.h>.
CPPFLAGS+=	-D_OPENBSD_SOURCE
# dhcpcd emits #warning's for kernel features MINIX's lwIP stack lacks
# (route-message filtering, RTM_MISS DST filtering, ND6 ra sending, route
# socket overflow detection); they are advisory, don't fail the build on them.
CPPFLAGS+=	-Wno-error=#warnings
.endif # defined(__MINIX)

# RFC 3118 authentication needs hmac(3)/<hmac.h>, which MINIX libc lacks.
.if !defined(__MINIX)
CPPFLAGS+=	-DAUTH
SRCS+=		auth.c
.endif

USE_INET?=	yes
.if (${USE_INET} != "no")
CPPFLAGS+=	-DINET
SRCS+=		bpf.c dhcp.c ipv4.c
CPPFLAGS+=	-DARP
SRCS+=		arp.c
CPPFLAGS+=	-DARPING
CPPFLAGS+=	-DIPV4LL
SRCS+=		ipv4ll.c
.endif

.if (${USE_INET6} != "no")
CPPFLAGS+=	-DINET6
SRCS+=		ipv6.c ipv6nd.c
CPPFLAGS+=	-DDHCP6
SRCS+=		dhcp6.c
.endif

CPPFLAGS+=	-I${SRCDIR}/include -I${DIST}/src

DPADD+=		${LIBUTIL}
LDADD+=		-lutil

.PATH: ${DIST}/src ${LIBC_NET}

CONFIGFILES=			dhcpcd.conf
FILESDIR_dhcpcd.conf=		/etc
FILESMODE_dhcpcd.conf=		644	# -rw-r--r--

# The hook runner and the individual hook scripts (9.4.1 ships them pre-built,
# no .in templating).
SCRIPTS=			dhcpcd-run-hooks
SCRIPTSDIR_dhcpcd-run-hooks=	/libexec

HOOKS=			01-test 10-wpa_supplicant 15-timezone
HOOKS+=			20-resolv.conf 29-lookup-hostname 30-hostname
HOOKS+=			50-ntp.conf

FILES=			${HOOKS:C,^,${DIST}/hooks/,}
FILESDIR=		/libexec/dhcpcd-hooks

.PATH: ${DIST}/hooks

MAN=			dhcpcd.conf.5 dhcpcd.8 dhcpcd-run-hooks.8

.include <bsd.prog.mk>
