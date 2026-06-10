#	$NetBSD: Makefile,v 1.1 2021/05/30 01:56:51 joerg Exp $

LIB=	LLVMDWARFLinker

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/lib/DWARFLinker
.PATH: ${LLVM_SRCDIR}/lib/DWARFLinker/Classic
.PATH: ${LLVM_SRCDIR}/lib/DWARFLinker/Parallel

SRCS+=	AcceleratorRecordsSaver.cpp \
	DIEAttributeCloner.cpp \
	DWARFEmitterImpl.cpp \
	DWARFLinker.cpp \
	DWARFLinkerBase.cpp \
	DWARFLinkerCompileUnit.cpp \
	DWARFLinkerDeclContext.cpp \
	DWARFLinkerImpl.cpp \
	DWARFLinkerTypeUnit.cpp \
	DWARFLinkerUnit.cpp \
	DWARFStreamer.cpp \
	DependencyTracker.cpp \
	OutputSections.cpp \
	SyntheticTypeNameBuilder.cpp \
	Utils.cpp

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
