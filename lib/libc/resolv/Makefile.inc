#	$NetBSD: Makefile.inc,v 1.5 2014/01/16 20:31:43 christos Exp $

# net sources
.PATH: ${.CURDIR}/resolv
.if defined(__MINIX)
# MINIX libc now provides pselect(2) (see sys/select.h + minix/lib/libc/sys/
# pselect.c), so res_send.c must NOT compile its private NEED_PSELECT shim --
# that would clash with the public prototype.  Use the real pselect(2).
CPPFLAGS+=-DCOMPAT__RES -DUSE_SELECT
.else
CPPFLAGS+=-DCOMPAT__RES -DUSE_POLL
.endif

SRCS+=	h_errno.c herror.c res_comp.c res_data.c res_debug.c \
	res_init.c res_mkquery.c res_query.c res_send.c \
	res_state.c mtctxres.c
# For COMPAT__RES
SRCS+=	res_compat.c
