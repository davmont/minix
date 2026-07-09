# $NetBSD$
#
# libfbclient: client-side helper for the framebuffer compositor
# (fbcompd).  A window client creates a SysV shm surface, draws into it,
# and talks to the compositor over an AF_UNIX socket.  Depends only on
# libc.  See fbclient.h and fbcomp_proto.h.

.include <bsd.own.mk>

LIB=		fbclient

SRCS=		fbclient.c

INCS=		fbclient.h fbcomp_proto.h
INCSDIR=	/usr/include

.include <bsd.lib.mk>
