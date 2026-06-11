#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:49 joerg Exp $

PROG_CXX=	sancov
NOMAN=		yes

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/tools/sancov

SRCS=	sancov.cpp

LLVM_LIBS+= \
	AArch64Disassembler \
	AArch64MCTargetDesc \
	AArch64TargetInfo \
	AArch64Utils \
	AMDGPUDisassembler \
	AMDGPUTargetInfo \
	AMDGPUMCTargetDesc \
	AMDGPUAsmParser \
	AMDGPUUtils \
	ARMDisassembler \
	ARMMCTargetDesc \
	ARMTargetInfo \
	ARMUtils \
	MipsDisassembler \
	MipsMCTargetDesc \
	MipsTargetInfo \
	PowerPCDisassembler \
	PowerPCMCTargetDesc \
	PowerPCTargetInfo \
	SparcDisassembler \
	SparcMCTargetDesc \
	SparcTargetInfo \
	X86Disassembler \
	X86CodeGen \
	X86MCTargetDesc \
	X86TargetInfo \
	MCDisassembler \
	DebugInfoSymbolize \
	DebugInfoDWARF \
	DebugInfoPDB \
	DebugInfoMSF \
	DebugInfoCodeView \
	Object \
	TextAPI \
	BitReader \
	BitstreamReader \
	IR \
	Remarks \
	MCParser \
	MC \
	BinaryFormat \
	Support \
	Demangle

LDADD+=	-lz
DPADD+=	${LIBZ}

.include "${.PARSEDIR}/../../driver.mk"

LLVM_LIBS+=	TargetParser

LLVM_LIBS+= \
	Option \
	TargetParser \
	CodeGenTypes \
	Analysis \
	FrontendHLSL \
	ProfileData \
	DebugInfoGSYM \
	DebugInfoBTF \
	IRReader \
	AsmParser

.include "${.PARSEDIR}/../../link.mk"

.include <bsd.prog.mk>
