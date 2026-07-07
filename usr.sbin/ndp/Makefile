# $NetBSD: Makefile,v 1.13 2017/10/10 19:30:41 christos Exp $

.include <bsd.own.mk>

USE_FORT?=	yes	# network client

# MINIX: gmt2local.c is vendored here; tcpdump 4.99 dropped it upstream.

RUMPPRG=	ndp
SRCS=		ndp.c gmt2local.c
MAN=		ndp.8

CPPFLAGS+=	-DINET6
CPPFLAGS+=	-I.

.if (${MKRUMP} != "no")
CPPFLAGS+= 	-DRUMP_ACTION
LDADD.rump+=	-lrumpres
DPADD.rump+=	${LIBRUMPRES}
.endif

.include <bsd.prog.mk>
