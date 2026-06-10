#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:52 joerg Exp $

LIB=	LLVMExecutionEngineJITLink

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/lib/ExecutionEngine/JITLink

SRCS+=	COFF.cpp \
	COFFDirectiveParser.cpp \
	COFFLinkGraphBuilder.cpp \
	COFF_x86_64.cpp \
	CompactUnwindSupport.cpp \
	DWARFRecordSectionSplitter.cpp \
	EHFrameSupport.cpp \
	ELF.cpp \
	ELFLinkGraphBuilder.cpp \
	ELF_aarch32.cpp \
	ELF_aarch64.cpp \
	ELF_loongarch.cpp \
	ELF_ppc64.cpp \
	ELF_riscv.cpp \
	ELF_systemz.cpp \
	ELF_x86.cpp \
	ELF_x86_64.cpp \
	JITLink.cpp \
	JITLinkGeneric.cpp \
	JITLinkMemoryManager.cpp \
	MachO.cpp \
	MachOLinkGraphBuilder.cpp \
	MachO_arm64.cpp \
	MachO_x86_64.cpp \
	XCOFF.cpp \
	XCOFFLinkGraphBuilder.cpp \
	XCOFF_ppc64.cpp \
	aarch32.cpp \
	aarch64.cpp \
	loongarch.cpp \
	ppc64.cpp \
	riscv.cpp \
	systemz.cpp \
	x86.cpp \
	x86_64.cpp

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
