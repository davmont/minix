#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:51 joerg Exp $

LIB=	LLVMDebugInfoGSYM

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/lib/DebugInfo/GSYM

SRCS+=	CallSiteInfo.cpp \
	DwarfTransformer.cpp \
	ExtractRanges.cpp \
	FileWriter.cpp \
	FunctionInfo.cpp \
	GsymContext.cpp \
	GsymCreator.cpp \
	GsymReader.cpp \
	Header.cpp \
	InlineInfo.cpp \
	LineTable.cpp \
	LookupResult.cpp \
	MergedFunctionsInfo.cpp \
	ObjectFileTransformer.cpp

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
