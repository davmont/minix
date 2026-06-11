#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:54 joerg Exp $

LIB=	LLVMTableGen

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/lib/TableGen

SRCS+=	DetailedRecordsBackend.cpp \
	Error.cpp \
	JSONBackend.cpp \
	Main.cpp \
	Parser.cpp \
	Record.cpp \
	SetTheory.cpp \
	StringMatcher.cpp \
	StringToOffsetTable.cpp \
	TGLexer.cpp \
	TGParser.cpp \
	TGTimer.cpp \
	TableGenBackend.cpp \
	TableGenBackendSkeleton.cpp

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
