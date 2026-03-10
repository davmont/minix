#	$NetBSD: Makefile,v 1.2 2012/04/12 01:05:31 mrg Exp $
#

GNUHOSTDIST=   ${.CURDIR}/../../external/lgpl3/gmp/dist

CONFIGURE_ARGS+=--disable-shared
CONFIGURE_ARGS+=--libdir=${TOOLDIR}/lib

.include "${.CURDIR}/../Makefile.gnuhost"

# Don't override CFLAGS and CXXFLAGS, it breaks ABI detection.
# PATH needs special treating since "make" tries to expand $PATH
# when variable is used.
CONFIGURE_ENV:=	${CONFIGURE_ENV:NPATH=*:NCFLAGS=*:NCXXFLAGS=*:NCC=*} \
		PATH="${TOOLDIR:Q}/bin:$$PATH" \
		CC=${HOST_CC:Q}\ -std=gnu89

.if ${HOST_OSTYPE:C/\-.*//} != "Minix"
# Force avoiding possibly non-executable install-sh.
CONFIGURE_ENV+=	ac_cv_path_mkdir="${TOOLDIR}/bin/${MACHINE_GNU_PLATFORM}-install -d"
.endif
