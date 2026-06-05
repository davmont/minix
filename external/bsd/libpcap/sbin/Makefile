# $NetBSD: Makefile,v 1.1 2020/03/29 17:12:45 christos Exp $

.if !defined(__MINIX)
# rpcapd needs libpthread, which MINIX libc doesn't ship.
SUBDIR=rpcapd
.endif

.include <bsd.subdir.mk>
