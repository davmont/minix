# Makefile for the vfat (FAT12/16/32, read-only) filesystem server
PROG=	vfat
SRCS=	main.c table.c mount.c inode.c fat.c direntry.c name.c \
	read.c write.c stadir.c utility.c

DPADD+=	${LIBMINIXFS} ${LIBFSDRIVER} ${LIBBDEV} ${LIBSYS}
LDADD+= -lminixfs -lfsdriver -lbdev -lsys

WARNS=3

.include <minix.service.mk>
