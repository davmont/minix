# $NetBSD: Makefile.inc,v 1.5 2019/07/30 08:05:22 martin Exp $

.include "../Makefile.inc"

BINDIR=		/bin

.if (${MKDYNAMICROOT} == "no")
LDSTATIC?=	-static
.endif

.if defined(__MINIX)
# MINIX libc has no libpthread; libcrypto here doesn't actually pull in
# pthread symbols at link time, so drop it from the deps.
DPADD=	${LIBARCHIVE_FE} ${LIBARCHIVE} ${LIBEXPAT} ${LIBBZ2} ${LIBLZMA} ${LIBZ} \
	${LIBCRYPTO}
LDADD=	-L${LIBARCHIVE_FE_DIR} -larchive_fe -larchive -lexpat -lbz2 -llzma -lz \
	-lcrypto
.else
DPADD=	${LIBARCHIVE_FE} ${LIBARCHIVE} ${LIBEXPAT} ${LIBBZ2} ${LIBLZMA} ${LIBZ} \
	${LIBCRYPTO} ${LIBPTHREAD}
LDADD=	-L${LIBARCHIVE_FE_DIR} -larchive_fe -larchive -lexpat -lbz2 -llzma -lz \
	-lcrypto -lpthread
.endif

CPPFLAGS+=	-I${LIBARCHIVEDIR}/libarchive_fe
