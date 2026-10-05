# Makefile for IPC server
PROG=	ipc
SRCS=	main.c utility.c shm.c posix_shm.c sem.c mqueue.c

DPADD+=	${LIBSYS} ${LIBTIMERS}
LDADD+=	-lsys -ltimers

FILES=ipc.conf
FILESNAME=ipc
FILESDIR= /etc/system.conf.d

WARNS?=	5

.include <minix.service.mk>
