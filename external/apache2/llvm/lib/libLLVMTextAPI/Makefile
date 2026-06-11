#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:54 joerg Exp $

LIB=	LLVMTextAPI

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/lib/TextAPI
.PATH: ${LLVM_SRCDIR}/lib/TextAPI/BinaryReader
SRCS+=	Architecture.cpp \
	ArchitectureSet.cpp \
	DylibReader.cpp \
	InterfaceFile.cpp \
	PackedVersion.cpp \
	Platform.cpp \
	RecordVisitor.cpp \
	RecordsSlice.cpp \
	Symbol.cpp \
	SymbolSet.cpp \
	Target.cpp \
	TextAPIError.cpp \
	TextStub.cpp \
	TextStubCommon.cpp \
	TextStubV5.cpp \
	Utils.cpp

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
