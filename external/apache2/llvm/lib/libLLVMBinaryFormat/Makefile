#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:51 joerg Exp $

LIB=	LLVMBinaryFormat

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/lib/BinaryFormat

SRCS+=	AMDGPUMetadataVerifier.cpp \
	COFF.cpp \
	DXContainer.cpp \
	Dwarf.cpp \
	ELF.cpp \
	MachO.cpp \
	Magic.cpp \
	MsgPackDocument.cpp \
	MsgPackDocumentYAML.cpp \
	MsgPackReader.cpp \
	MsgPackWriter.cpp \
	SFrame.cpp \
	Wasm.cpp \
	XCOFF.cpp

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
