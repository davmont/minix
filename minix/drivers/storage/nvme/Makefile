# Makefile for the NVM Express (NVMe) storage driver
PROG=	nvme
SRCS=	nvme.c

DPADD+=	${LIBBLOCKDRIVER} ${LIBSYS} ${LIBTIMERS}
LDADD+=	-lblockdriver -lsys -ltimers

.include <minix.service.mk>
