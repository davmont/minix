#	$NetBSD: Makefile.inc,v 1.12.2.2 2024/02/26 08:32:24 martin Exp $

.if !defined(BIND9_MAKEFILE_INC)
BIND9_MAKEFILE_INC=yes

#NAMED_DEBUG=1

USE_FORT?= yes	# network client/server

WARNS?=	1

CWARNFLAGS.clang+=	-Wno-unused-value -Wno-parentheses
# -Wno-atomic-alignment is not understood by MINIX's older in-tree clang.

.include <bsd.own.mk>

LIBUVDIR=	${NETBSDSRCDIR}/external/mit/libuv
# The libuv library is built in lib/libuv (a SUBDIR), not lib/ itself.
LIBUVOBJDIR!=   cd ${LIBUVDIR}/lib/libuv && ${PRINTOBJDIR}
CPPFLAGS+=	-I${LIBUVDIR}/dist/include
# BIND 9.18 needs C11 (max_align_t, _Atomic, _Noreturn).  Set CSTD so the value
# wins over share/mk/bsd.sys.mk's default -std (a bare CFLAGS+=-std=gnu18 loses,
# because bsd.sys.mk appends its -std after this file's CFLAGS).
CSTD=		gnu18
LINTFLAGS+=	-Ac11

BIND_SRCDIR:=	${.PARSEDIR}
IDIST=		${BIND_SRCDIR}/dist
BIND_HTMLDIR=	/usr/share/doc/reference/ref8/bind9

SYSCONFDIR=/etc
LOCALSTATEDIR=/var

LINTFLAGS+=-Z-include -Z${BIND_SRCDIR}/include/config.h
CPPFLAGS+=-include ${BIND_SRCDIR}/include/config.h
CPPFLAGS+=-I${BIND_SRCDIR}/include \
	-I${IDIST} \
	-I${IDIST}/lib/bind9/include \
	-I${IDIST}/lib/dns/include \
	-I${IDIST}/lib/ns/include \
	-I${IDIST}/lib/irs/include \
	-I${IDIST}/lib/isc/include -I${IDIST}/lib/isc/unix/include \
	-I${IDIST}/lib/isccfg/include \
	-I${IDIST}/lib/isccc/include \
	-DSESSION_KEYFILE=\"${LOCALSTATEDIR}/run/named/session.key\" \
	-DNAMED_CONFFILE=\"${SYSCONFDIR}/named.conf\"

.if defined(HAVE_GCC)
COPTS+=	-Wno-pointer-sign
.endif

.if defined(NAMED_DEBUG)
DBG=-g3 -gstabs
.endif

.if !defined(LIB) || empty(LIB)
# NOTE: the order of these libraries is important...
.if defined(NAMED_DEBUG)
LDADD+=		-lbind9_g -lisccfg_g -ldns_g -lns_g -lirs_g
LDADD+=		-lisccc_g -lisc_g
LDADD+=		-L${LIBUVOBJDIR} -luv_g
LDADD+=		-lexecinfo_g -lelf_g -lkvm_g -lz_g
.else
LDADD+=		-lbind9 -lisccfg -ldns -lns -lirs
DPADD+=		${LIBBIND9} ${LIBISCCFG} ${LIBDNS} ${LIBNS} ${LIBIRS}
LDADD+=		-lisccc -lisc 
LDADD+=		-L${LIBUVOBJDIR} -luv
LDADD+=		-lexecinfo -lelf -lkvm -lz
DPADD+=		${LIBISCCC} ${LIBISC}
DPADD+=		${LIBUVOBJDIR}/libuv.a
DPADD+=		${LIBEXECINFO} ${LIBKVM} ${LIBZ}
.endif
.endif

# NetBSD renamed libblacklist -> libblocklist; MINIX still has the old name.
.if defined(__MINIX)
LDADD+=-lblacklist
DPADD+=${LIBBLACKLIST}
.else
LDADD+=-lblocklist
DPADD+=${LIBBLOCKLIST}
.endif

.if !defined (LIB) || empty(LIB)
LDADD+= -lpthread
DPADD+= ${LIBPTHREAD}
.else
LIBDPLIBS+=      pthread  ${NETBSDSRCDIR}/lib/libpthread
.endif

.if ${MKKERBEROS} != "no"
.if !defined (LIB) || empty(LIB)
LDADD+= -lgssapi -lheimntlm ${LIBKRB5_LDADD}
DPADD+= ${LIBGSSAPI} ${LIBHEIMNTLM} ${LIBKRB5_DPADD}
.else
.for L in gssapi krb5 hx509 heimntlm heimbase com_err roken asn1 wind
LIBDPLIBS+=	$L ${NETBSDSRCDIR}/crypto/external/bsd/heimdal/lib/lib$L
.endfor
.endif
.endif

.if !defined (LIB) || empty(LIB)
LDADD+= -lssl -lcrypto -lcrypt
DPADD+= ${LIBSSL} ${LIBCRYPTO} ${LIBCRYPT}
.else
LIBDPLIBS+=	crypto ${NETBSDSRCDIR}/crypto/external/bsd/${EXTERNAL_OPENSSL_SUBDIR}/lib/libcrypto
.endif
.endif

CPPFLAGS+=-I${IDIST}/lib/isc/pthreads/include

.if exists(${.PARSEDIR}/../Makefile.inc)
.include "${.PARSEDIR}/../Makefile.inc"
.endif

COPTS+=	-fcommon
