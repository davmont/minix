# Makefile for the exFAT (read-only) filesystem server
PROG=	exfat
SRCS=	main.c table.c mount.c inode.c cluster.c dir.c name.c \
	read.c stadir.c utility.c

DPADD+=	${LIBMINIXFS} ${LIBFSDRIVER} ${LIBBDEV} ${LIBSYS}
LDADD+= -lminixfs -lfsdriver -lbdev -lsys

WARNS=3

.include <minix.service.mk>
