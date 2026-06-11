#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:46 joerg Exp $

PROG_CXX=	lli
NOMAN=		yes

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/tools/lli

SRCS=	lli.cpp \
	ExecutionUtils.cpp

LLVM_LIBS+= \
	Orc \
	ExecutionEngineJITLink \
	ExecutionEngineOrcTargetProcess \
	ExecutionEngineOrcShared \
	X86CodeGen \
	X86TargetInfo \
	X86MCTargetDesc \
	X86AsmParser \
	TransformsCFGuard \
	AsmPrinter \
	GlobalISel \
	IRReader \
	AsmParser \
	DebugInfoCodeView \
	DebugInfoDWARF \
	Object \
	TextAPI \
	MCParser \
	BitWriter \
	BitReader \
	BitstreamReader \
	MCDisassembler \
	MCJIT \
	RuntimeDyld \
	Instrumentation \
	Interpreter \
	ExecutionEngine \
	SelectionDAG \
	CodeGen \
	ProfileData \
	ScalarOpts \
	TransformsUtils \
	Analysis \
	Target \
	MC \
	IR \
	Remarks \
	BinaryFormat \
	Support \
	Demangle

LDADD+=	-lz
DPADD+=	${LIBZ}

LLVM_LIBS+=	TargetParser

LLVM_LIBS+= \
	Passes \
	TargetParser \
	X86Disassembler \
	TransformsCoroutines \
	HipStdPar \
	ipo \
	FrontendOpenMP \
	FrontendOffloading \
	ObjectYAML \
	FrontendAtomic \
	FrontendDirective \
	Linker \
	Vectorize \
	SandboxIR \
	WindowsDriver \
	Option \
	IRPrinter \
	CGData \
	ObjCARC \
	TransformsAggressiveInstCombine \
	InstCombine \
	FrontendHLSL \
	DebugInfoSymbolize \
	DebugInfoGSYM \
	DebugInfoPDB \
	DebugInfoMSF \
	DebugInfoBTF \
	CodeGenTypes

.include "${.PARSEDIR}/../../link.mk"

.include <bsd.prog.mk>
