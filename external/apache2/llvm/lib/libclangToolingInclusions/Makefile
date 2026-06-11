#	$NetBSD: Makefile,v 1.1 2019/11/11 22:45:17 joerg Exp $

LIB=	clangToolingInclusions

.include <bsd.init.mk>

.PATH: ${CLANG_SRCDIR}/lib/Tooling/Inclusions
.PATH: ${CLANG_SRCDIR}/lib/Tooling/Inclusions/Stdlib

SRCS+=	HeaderAnalysis.cpp \
	HeaderIncludes.cpp \
	IncludeStyle.cpp \
	StandardLibrary.cpp

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
