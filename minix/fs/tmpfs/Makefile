# Makefile for the in-memory file system (tmpfs)
PROG=	tmpfs
SRCS=	tmpfs.c

DPADD+=	${LIBFSDRIVER} ${LIBSYS}
LDADD+=	-lfsdriver -lsys

MAN=	tmpfs.8

.include <minix.service.mk>
