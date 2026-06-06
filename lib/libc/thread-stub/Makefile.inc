#	$NetBSD: Makefile.inc,v 1.4 2013/04/05 20:15:42 christos Exp $

.include <bsd.own.mk>

# Our sources
.PATH: ${.CURDIR}/thread-stub

SRCS+=	__isthreaded.c thread-stub.c thread-stub-init.c

.if defined(__MINIX)
# MINIX builds libc without _REENTRANT, so the thread-stub layer (which is
# guarded by #ifdef _REENTRANT) would compile to nothing and libpthread could
# not resolve the __libc_*_stub symbols / weak aliases it overrides.  Build
# just these files with _REENTRANT so the stubs exist; the rest of libc stays
# non-reentrant.
COPTS.thread-stub.c+=		-D_REENTRANT
COPTS.thread-stub-init.c+=	-D_REENTRANT
COPTS.__isthreaded.c+=		-D_REENTRANT
.endif
