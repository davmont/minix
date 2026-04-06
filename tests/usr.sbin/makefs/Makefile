# $NetBSD$

.include <bsd.own.mk>

TESTSDIR=	${TESTSBASE}/usr.sbin/makefs

TESTS_C=	t_cd9660_strings

SRCS.t_cd9660_strings=	t_cd9660_strings.c cd9660_strings.c

.PATH:		${NETBSDSRCDIR}/usr.sbin/makefs/cd9660
CPPFLAGS+=	-I${NETBSDSRCDIR}/usr.sbin/makefs -I${NETBSDSRCDIR}/sys/fs/cd9660 -I${NETBSDSRCDIR}/usr.sbin/makefs/cd9660

.include <bsd.test.mk>
