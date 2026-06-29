#	$NetBSD: Makefile,v 1.1 2020/07/22 19:00:19 roy Exp $

SUBDIR=		resolvconf
# libc.d is only built when its source directory is present in the tree.
# The openresolv 3.12 resync added this SUBDIR but never imported libc.d/,
# so guard it: a missing dir must not abort obj/cleandir/build.
.if exists(${.CURDIR}/libc.d)
SUBDIR+=	libc.d
.endif

.include <bsd.subdir.mk>
