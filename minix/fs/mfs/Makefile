# Makefile for Minix File System (MFS)
PROG=	mfs
SRCS=	cache.c dirhash.c journal.c link.c \
	mount.c misc.c open.c protect.c read.c \
	stadir.c stats.c table.c time.c utility.c \
	write.c inode.c main.c path.c super.c xattr.c

DPADD+=	${LIBMINIXFS} ${LIBFSDRIVER} ${LIBBDEV} ${LIBSYS} ${LIBMTHREAD}
LDADD+= -lminixfs -lfsdriver -lbdev -lsys -lmthread

CPPFLAGS+= -DDEFAULT_NR_BUFS=1024

.include <minix.service.mk>
