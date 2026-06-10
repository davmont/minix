#	$NetBSD: Makefile,v 1.1 2019/11/11 22:45:11 joerg Exp $

LIB=	LLVMProfileData

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/lib/ProfileData

SRCS+=	DataAccessProf.cpp \
	GCOV.cpp \
	IndexedMemProfData.cpp \
	InstrProf.cpp \
	InstrProfCorrelator.cpp \
	InstrProfReader.cpp \
	InstrProfWriter.cpp \
	ItaniumManglingCanonicalizer.cpp \
	MemProf.cpp \
	MemProfCommon.cpp \
	MemProfRadixTree.cpp \
	MemProfReader.cpp \
	MemProfSummary.cpp \
	MemProfSummaryBuilder.cpp \
	PGOCtxProfReader.cpp \
	PGOCtxProfWriter.cpp \
	ProfileSummaryBuilder.cpp \
	SampleProf.cpp \
	SampleProfReader.cpp \
	SampleProfWriter.cpp \
	SymbolRemappingReader.cpp

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
