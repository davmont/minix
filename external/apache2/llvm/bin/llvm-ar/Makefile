#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:47 joerg Exp $

PROG_CXX=	llvm-ar
NOMAN=		yes

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/tools/llvm-ar

SRCS=	llvm-ar.cpp

LLVM_LIBS+= \
	ToolDriversDlltool \
	ToolDrivers \
	AArch64AsmParser \
	AArch64MCTargetDesc \
	AArch64TargetInfo \
	AArch64Utils \
	AMDGPUMCTargetDesc \
	AMDGPUTargetInfo \
	AMDGPUAsmParser \
	AMDGPUUtils \
	ARMAsmParser \
	ARMMCTargetDesc \
	ARMTargetInfo \
	ARMUtils \
	MipsAsmParser \
	MipsMCTargetDesc \
	MipsTargetInfo \
	PowerPCAsmParser \
	PowerPCMCTargetDesc \
	PowerPCTargetInfo \
	SparcTargetInfo \
	SparcMCTargetDesc \
	SparcAsmParser \
	X86AsmParser \
	X86CodeGen \
	X86MCTargetDesc \
	X86TargetInfo \
	CodeGen \
	MCDisassembler \
	Object \
	MCParser \
	MC \
	BitReader \
	BitstreamReader \
	IR \
	Remarks \
	BinaryFormat \
	Option \
	Support \
	Demangle

LDADD+=	-lz
DPADD+=	${LIBZ}

LLVM_DRIVER_TOOL=	ar
.include "${.PARSEDIR}/../../driver.mk"

LLVM_LIBS+=	TargetParser

LLVM_LIBS+= \
	TargetParser \
	CodeGenTypes \
	Analysis \
	FrontendHLSL \
	ProfileData \
	DebugInfoSymbolize \
	DebugInfoGSYM \
	DebugInfoPDB \
	DebugInfoCodeView \
	DebugInfoMSF \
	DebugInfoBTF \
	DebugInfoDWARF \
	IRReader \
	AsmParser \
	TextAPI

.include "${.PARSEDIR}/../../link.mk"

.include <bsd.prog.mk>
