#	$NetBSD: Makefile,v 1.1 2021/05/30 01:56:48 joerg Exp $

PROG_CXX=	llvm-size
NOMAN=		yes

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/tools/llvm-reduce

SRCS=	DeltaManager.cpp \
	TestRunner.cpp \
	llvm-reduce.cpp

.PATH: ${LLVM_SRCDIR}/tools/llvm-reduce/deltas

SRCS+=	Delta.cpp \
	ReduceAliases.cpp \
	ReduceArguments.cpp \
	ReduceAttributes.cpp \
	ReduceBasicBlocks.cpp \
	ReduceFunctionBodies.cpp \
	ReduceFunctions.cpp \
	ReduceGlobalValues.cpp \
	ReduceGlobalVarInitializers.cpp \
	ReduceGlobalVars.cpp \
	ReduceInstructions.cpp \
	ReduceMetadata.cpp \
	ReduceModuleInlineAsm.cpp \
	ReduceOperandBundles.cpp \
	ReduceSpecialGlobals.cpp


LLVM_LIBS+= \
	Object \
	TextAPI \
	MCParser \
	MC \
	BitReader \
	BitstreamReader \
	IR \
	Remarks \
	BinaryFormat \
	Support \
	Demangle

LLVM_LIBS+=	TargetParser

LLVM_LIBS+= \
	X86AsmParser \
	PowerPCAsmParser \
	SparcAsmParser \
	AArch64AsmParser \
	ARMAsmParser \
	MipsAsmParser \
	AMDGPUAsmParser \
	X86CodeGen \
	PowerPCCodeGen \
	SparcCodeGen \
	AArch64CodeGen \
	ARMCodeGen \
	MipsCodeGen \
	AMDGPUCodeGen \
	X86MCTargetDesc \
	PowerPCMCTargetDesc \
	SparcMCTargetDesc \
	AArch64MCTargetDesc \
	ARMMCTargetDesc \
	MipsMCTargetDesc \
	AMDGPUMCTargetDesc \
	X86TargetInfo \
	PowerPCTargetInfo \
	SparcTargetInfo \
	AArch64TargetInfo \
	ARMTargetInfo \
	MipsTargetInfo \
	AMDGPUTargetInfo \
	Analysis \
	BitWriter \
	CodeGen \
	CodeGenTypes \
	ipo \
	IRReader \
	MIRParser \
	Passes \
	Target \
	TransformsUtils \
	AArch64Utils \
	MCDisassembler \
	ARMUtils \
	TransformsCFGuard \
	TransformsCoroutines \
	FrontendOpenMP \
	FrontendOffloading \
	ObjectYAML \
	FrontendAtomic \
	FrontendDirective \
	Linker \
	AMDGPUUtils \
	AsmPrinter \
	GlobalISel \
	IRPrinter \
	Instrumentation \
	SelectionDAG \
	ObjCARC \
	ScalarOpts \
	TransformsAggressiveInstCombine \
	InstCombine \
	CGData \
	Vectorize \
	SandboxIR \
	HipStdPar \
	ProfileData \
	DebugInfoSymbolize \
	DebugInfoGSYM \
	DebugInfoDWARF \
	DebugInfoPDB \
	DebugInfoCodeView \
	DebugInfoMSF \
	DebugInfoBTF \
	AsmParser \
	FrontendHLSL

.include "${.PARSEDIR}/../../link.mk"

.include <bsd.prog.mk>
