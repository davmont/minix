#	$NetBSD: Makefile.inc,v 1.19 2026/03/15 17:42:51 rillig Exp $

.if !defined(BIND9_MAKEFILE_INC)
BIND9_MAKEFILE_INC=yes

#NAMED_DEBUG=1

USE_FORT?= yes	# network client/server

WARNS?=	1

CWARNFLAGS.clang+=	-Wno-unused-value -Wno-parentheses \
			-Wno-atomic-alignment

.include <bsd.own.mk>

# MINIX: BIND ships its manuals as Sphinx reStructuredText; we do not build
# them (keeps the man set lists unchanged).
MKMAN=	no

LIBUVDIR=	${NETBSDSRCDIR}/external/mit/libuv
# MINIX: the libuv library is built in lib/libuv (a SUBDIR), not lib/ itself.
LIBUVOBJDIR!=   cd ${LIBUVDIR}/lib/libuv && ${PRINTOBJDIR}
LIBURCUDIR=	${NETBSDSRCDIR}/external/lgpl2/userspace-rcu
LIBURCUMBOBJDIR!=   cd ${LIBURCUDIR}/lib/liburcu-mb && ${PRINTOBJDIR}
LIBURCUCDSOBJDIR!=   cd ${LIBURCUDIR}/lib/liburcu-cds && ${PRINTOBJDIR}
LIBURCUCOMMONOBJDIR!=   cd ${LIBURCUDIR}/lib/liburcu-common && ${PRINTOBJDIR}
CPPFLAGS+=	-I${LIBUVDIR}/dist/include
CPPFLAGS+=	-I${LIBURCUDIR}/dist/include -I${LIBURCUDIR}/include
# MINIX: BIND needs C11+.  Set CSTD so the value wins over bsd.sys.mk's
# default -std (a bare CFLAGS+=-std=gnu18 loses; bsd.sys.mk appends later).
CSTD=		gnu18
LINTFLAGS+=	-Ac11

LINTFLAGS+=	-X 220	# fallthrough on case statement
LINTFLAGS+=	-X 226	# static variable 'X' unused (used for constexpr)
LINTFLAGS+=	-X 247	# pointer cast from 'X' to unrelated 'Y' (isc__magic_t)

BIND_SRCDIR:=	${.PARSEDIR}
IDIST=		${BIND_SRCDIR}/dist
BIND_HTMLDIR=	/usr/share/doc/reference/ref8/bind9

SYSCONFDIR=/etc
LOCALSTATEDIR=/var

LINTFLAGS+=-Z-include -Z${BIND_SRCDIR}/include/config.h
CPPFLAGS+=-include ${BIND_SRCDIR}/include/config.h
CPPFLAGS+=-I${BIND_SRCDIR}/include \
	-I${IDIST} \
	-I${IDIST}/lib/dns/include \
	-I${IDIST}/lib/ns/include \
	-I${IDIST}/lib/isc/include \
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
LDADD+=		-lisccfg_g -ldns_g -lns_g
LDADD+=		-lisccc_g -lisc_g
LDADD+=		-L${LIBURCUMBOBJDIR} -lurcu-mb_g
LDADD+=		-L${LIBURCUCDSOBJDIR} -lurcu-cds_g
LDADD+=		-L${LIBURCUCOMMONOBJDIR} -lurcu-common_g
LDADD+=		-L${LIBUVOBJDIR} -luv_g
LDADD+=		-lexecinfo_g -lelf_g -lkvm_g -lz_g -lm_g
.else
LDADD+=		-lisccfg -ldns -lns
DPADD+=		${LIBISCCFG} ${LIBDNS} ${LIBNS}
LDADD+=		-lisccc -lisc 
LDADD+=		-L${LIBURCUMBOBJDIR} -lurcu-mb
LDADD+=		-L${LIBURCUCDSOBJDIR} -lurcu-cds
LDADD+=		-L${LIBURCUCOMMONOBJDIR} -lurcu-common
LDADD+=		-L${LIBUVOBJDIR} -luv
LDADD+=		-lexecinfo -lelf -lkvm -lz -lm
DPADD+=		${LIBISCCC} ${LIBISC}
DPADD+=		${LIBUVOBJDIR}/libuv.a
DPADD+=		${LIBEXECINFO} ${LIBKVM} ${LIBZ} ${LIBM}
.endif
.endif

# MINIX: NetBSD renamed libblacklist -> libblocklist; MINIX has the old name
# (include/blocklist.h shims the API names).
.if defined(__MINIX)
LDADD+=-lblacklist
DPADD+=${LIBBLACKLIST}
.else
LDADD+=-lblocklist
.endif
DPADD+=${LIBBLOCKLIST}

.if !defined (LIB) || empty(LIB)
LDADD+= -lpthread
DPADD+= ${LIBPTHREAD}
.else
LIBDPLIBS+=      pthread  ${NETBSDSRCDIR}/lib/libpthread
.endif

.if ${MKKERBEROS} != "no"
CPPFLAGS+=-DWITH_KERBEROS
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
# MINIX: OpenSSL lives under crypto/external/bsd/ here (NetBSD-current moved
# theirs to crypto/external/apache2/ with the 3.5 relicense).
LIBDPLIBS+=	crypto ${NETBSDSRCDIR}/crypto/external/${EXTERNAL_OPENSSL_SUBDIR}/lib/libcrypto
.endif
.endif

CPPFLAGS+=-I${IDIST}/lib/isc/pthreads/include

.if exists(${.PARSEDIR}/../Makefile.inc)
.include "${.PARSEDIR}/../Makefile.inc"
.endif

COPTS+=	-fcommon

# MINIX links programs statically (no DT_INIT), so libisc's library
# constructor isc__initialize() would be garbage-collected out, leaving its
# globals uninitialized.  Force the object in.
.if defined(__MINIX)
LDFLAGS+= -Wl,-u,isc__initialize
.endif
