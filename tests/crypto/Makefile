# $NetBSD: Makefile,v 1.5 2014/01/15 10:19:55 martin Exp $

.include <bsd.own.mk>

.if !defined(__MINIX)
# The OpenSSL libcrypto unit tests link OpenSSL-internal ossl_* symbols that
# our libcrypto.so does not export (3.0 restructured the test framework);
# skip them on MINIX until the test linkage is sorted out.
TESTS_SUBDIRS=		libcrypto
TESTS_SUBDIRS+=		opencrypto
.endif # !defined(__MINIX)

TESTSDIR=	${TESTSBASE}/crypto

.include <bsd.test.mk>
