# Makefile for the NVM Express (NVMe) storage driver
PROG=	nvme
SRCS=	nvme.c

DPADD+=	${LIBBLOCKDRIVER} ${LIBSYS} ${LIBTIMERS} ${LIBMTHREAD}
LDADD+=	-lblockdriver -lsys -ltimers -lmthread

.include <minix.service.mk>
