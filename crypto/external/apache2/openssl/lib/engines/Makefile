#	$NetBSD: Makefile,v 1.1 2025/07/17 14:25:21 christos Exp $

.include "bsd.own.mk"

# OpenSSL engines.
.if defined(__MINIX)
# devcrypto needs <crypto/cryptodev.h> (Linux/BSD /dev/crypto) which
# MINIX doesn't provide; dasync uses libpthread.  Skip both on MINIX.
SUBDIR= loader_attic ossltest
.else
SUBDIR= dasync devcrypto loader_attic ossltest
.endif

# VIA Padlock -- only makes sense for x86 which might be a VIA CPU
.if ${MACHINE_ARCH} == "i386" || ${MACHINE_ARCH} == "x86_64"
SUBDIR+=padlock
.endif

.include <bsd.subdir.mk>
