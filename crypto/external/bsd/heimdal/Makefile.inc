# $NetBSD: Makefile.inc,v 1.6.6.2 2023/10/14 06:56:35 martin Exp $

HEIMBASE?=	${NETBSDSRCDIR}/crypto/external/bsd/heimdal
HEIMDIST=	${HEIMBASE}/dist

.if defined(__MINIX)
# NOTE: the Kerberos tools are built STATICALLY here (MINIX's default).  A
# previous attempt to build them dynamically -- so they could dlopen OpenSSL
# 3's loadable legacy provider and thus restore des3-cbc-sha1 / arcfour-hmac-md5
# -- broke the clean build: dynamic heimdal programs need the build-tree lib
# dirs on -Wl,-rpath-link to resolve transitive shared-lib deps (e.g.
# libgssapi.so -> libheimntlm.so), but MINIX's bsd.prog.mk only emits
# -rpath-link when SHLINKDIR != /usr/libexec (it is /usr/libexec here), and
# PROGDPLIBS adds only -L/-l, not -rpath-link.  So programs failed to link with
# "libgssapi.so: undefined reference to heim_ntlm_*".  Restoring those legacy
# enctypes therefore depends on the broader static->dynamic migration (which
# must fix transitive -rpath-link); until then the tools stay static and the
# default key set is AES-only (see lib/hdb/keys.c).

# Disable ELF symbol versioning for the Heimdal shared libraries.  The shared
# rule in Makefile.rules.inc gates versioning on a *parse-time*
# `exists(version-script.map)', but that map is generated at build time -- so a
# clean build leaves some libs unversioned while consumers (e.g. libgssapi) end
# up with versioned references, which then fail to link:
#   libgssapi.so: undefined reference to `heim_ntlm_*@HEIMDAL_NTLM_1.0'
# MINIX's Heimdal is new and has no ABI-compatibility constraints, so a single
# unversioned ABI is both correct and deterministic across clean/incremental
# builds.  (Defined before Makefile.rules.inc is pulled in by each lib.)
NO_VERSION_SCRIPT=	yes
.endif

SSLBASE?=	${NETBSDSRCDIR}/crypto/external/bsd/${EXTERNAL_OPENSSL_SUBDIR}

DPLIBASN1=	asn1 ${HEIMBASE}/lib/libasn1
DPLIBCOM_ERR=	com_err ${HEIMBASE}/lib/libcom_err
DPLIBGSSAPI=	gssapi ${HEIMBASE}/lib/libgssapi
DPLIBHDB=	hdb ${HEIMBASE}/lib/libhdb
DPLIBHEIMBASE=	heimbase ${HEIMBASE}/lib/libheimbase
DPLIBHEIMNTLM=	heimntlm ${HEIMBASE}/lib/libheimntlm
DPLIBHX509=	hx509 ${HEIMBASE}/lib/libhx509
DPLIBIPC=	ipc ${HEIMBASE}/lib/libipc
DPLIBKADM5CLNT=	kadm5clnt ${HEIMBASE}/lib/libkadm5clnt
DPLIBKADM5SRV=	kadm5srv ${HEIMBASE}/lib/libkadm5srv
DPLIBKAFS=	kafs ${HEIMBASE}/lib/libkafs
DPLIBKDC=	kdc ${HEIMBASE}/lib/libkdc
DPLIBKRB5=	krb5 ${HEIMBASE}/lib/libkrb5
DPLIBROKEN=	roken ${HEIMBASE}/lib/libroken
DPLIBSL=	sl ${HEIMBASE}/lib/libsl
DPLIBVERS=	vers ${HEIMBASE}/lib/libvers
DPLIBWIND=	wind ${HEIMBASE}/lib/libwind
KRB5LDADD=	-lcrypto -lcrypt
KRB5DPADD=	${LIBCRYPTO} ${LIBCRYPT}
HDBLDADD=	-lsqlite3 -lm
HDBDPADD=	${LIBSQLITE3} ${LIBM}

.if ${USETOOLS} != "yes"
COMPILEETOBJ!=     cd ${HEIMBASE}/lib/libcom_err/compile_et && ${PRINTOBJDIR}
TOOL_COMPILE_ET=   ${COMPILEETOBJ}/compile_et

ASN1COMPILEOBJ!=   cd ${HEIMBASE}/lib/libasn1/asn1_compile && ${PRINTOBJDIR}
TOOL_ASN1_COMPILE= ${ASN1COMPILEOBJ}/asn1_compile

SLCOBJ!=	cd ${HEIMBASE}/lib/libsl/slc && ${PRINTOBJDIR}
TOOL_SLC=	${SLCOBJ}/slc
.endif

#
# We now define what are standard CPP flags across Heimdal:

.if (${USE_INET6} != "no")
CPPFLAGS+=-DHAVE_IPV6
HOST_CPPFLAGS+=-DHAVE_IPV6
.endif

.if ${MKPIC} == "no"
CPPFLAGS+=	-DNO_DLOPEN
.endif

CPPFLAGS+=	-DHAVE_CONFIG_H
HOST_CPPFLAGS+=	-DHAVE_CONFIG_H

CPPFLAGS+= ${.PATH:S/^/-I/}
CPPFLAGS+= -I${HEIMBASE}/include
CPPFLAGS+= -I${HEIMDIST}/include
CWARNFLAGS.clang+=	-Wno-error=absolute-value
