#	$NetBSD: Makefile,v 1.58 2015/06/17 17:43:23 christos Exp $
#	@(#)Makefile	5.2 (Berkeley) 12/28/90

PROG=	make
SRCS=	arch.c buf.c compat.c cond.c dir.c for.c hash.c job.c main.c \
	make.c metachar.c parse.c str.c suff.c targ.c trace.c var.c util.c 
SRCS+=  make_malloc.c
SRCS+=  lst.c

# let people experiment for a bit
USE_META ?= no
.if ${USE_META:tl} != "no"
SRCS+=	meta.c
CPPFLAGS+= -DUSE_META
FILEMON_H ?= ${.CURDIR:H:H}/sys/dev/filemon/filemon.h
.if exists(${FILEMON_H}) && ${FILEMON_H:T} == "filemon.h"
COPTS.meta.c += -DHAVE_FILEMON_H -I${FILEMON_H:H}
.endif
.endif

.if defined(__MINIX)
CPPFLAGS+= -DMAKE_MACHINE=\"${MACHINE}\" -DMAKE_MACHINE_ARCH=\"${MACHINE_ARCH}\"
.endif #defined(__MINIX)
.if !defined(__MINIX)
SUBDIR=	PSD.doc
.endif #!defined(__MINIX)
.if make(obj) || make(clean)
SUBDIR+= unit-tests
.endif

.include <bsd.prog.mk>
.include <bsd.subdir.mk>

CPPFLAGS+= -DMAKE_NATIVE
COPTS.var.c += -Wno-cast-qual
COPTS.job.c += -Wno-format-nonliteral
COPTS.parse.c += -Wno-format-nonliteral
COPTS.var.c += -Wno-format-nonliteral

.ifdef TOOLDIR
# this is a native netbsd build, 
# use libutil rather than the local emalloc etc.
CPPFLAGS+= -DUSE_EMALLOC
LDADD+=-lutil
DPADD+=${LIBUTIL}
.endif

# A simple unit-test driver to help catch regressions
accept test:
	cd ${.CURDIR}/unit-tests && MAKEFLAGS= ${.MAKE} -r -m / TEST_MAKE=${TEST_MAKE:U${.OBJDIR}/${PROG:T}} ${.TARGET}
