.include <bsd.own.mk>

PROG=	trace
SRCS=	call.c error.c escape.c format.c ioctl.c kernel.c mem.c output.c \
	proc.c signal.c trace.c
.PATH:	${.CURDIR}/service
SRCS+=	pm.c vfs.c rs.c mib.c vm.c ipc.c
.PATH:	${.CURDIR}/ioctl
SRCS+=	block.c char.c net.c svrctl.c

CPPFLAGS+=	-D_MINIX_SYSTEM=1 -I${.CURDIR} -I${NETBSDSRCDIR}/minix

COPTS.format.c+=	-Wno-format-nonliteral

error.c: error.awk ${NETBSDSRCDIR}/sys/sys/errno.h
	${TOOL_AWK} -f ${.ALLSRC} > ${.TARGET}

signal.c: signal.awk ${NETBSDSRCDIR}/sys/sys/signal.h
	${TOOL_AWK} -f ${.ALLSRC} > ${.TARGET}

CLEANFILES+=	error.c signal.c

WARNS?=	5

.include <bsd.prog.mk>
