#	$NetBSD: Makefile,v 1.1 2021/05/30 01:56:51 joerg Exp $

LIB=	LLVMDWARFLinker

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/lib/DWARFLinker

SRCS+=	Classic/DWARFLinker.cpp \
	Classic/DWARFLinkerCompileUnit.cpp \
	Classic/DWARFLinkerDeclContext.cpp \
	Classic/DWARFStreamer.cpp \
	DWARFLinkerBase.cpp \
	Parallel/AcceleratorRecordsSaver.cpp \
	Parallel/DIEAttributeCloner.cpp \
	Parallel/DWARFEmitterImpl.cpp \
	Parallel/DWARFLinker.cpp \
	Parallel/DWARFLinkerCompileUnit.cpp \
	Parallel/DWARFLinkerImpl.cpp \
	Parallel/DWARFLinkerTypeUnit.cpp \
	Parallel/DWARFLinkerUnit.cpp \
	Parallel/DependencyTracker.cpp \
	Parallel/OutputSections.cpp \
	Parallel/SyntheticTypeNameBuilder.cpp \
	Utils.cpp

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
