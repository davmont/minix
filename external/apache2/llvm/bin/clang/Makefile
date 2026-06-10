#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:46 joerg Exp $

PROG_CXX=	clang
NOMAN=		yes

.if !defined(HOSTPROG)
SYMLINKS+=	clang ${BINDIR}/clang++
SYMLINKS+=	clang ${BINDIR}/clang-cpp
.endif

.include <bsd.init.mk>

.PATH: ${CLANG_SRCDIR}/tools/driver

SRCS=	driver.cpp \
	cc1_main.cpp \
	cc1as_main.cpp \
	cc1gen_reproducer_main.cpp \
	clang-driver.cpp

# LLVM 22 moved the tool's main() into a generated wrapper that calls
# clang_main(); produce it from the shared llvm-driver template.
CLEANFILES+=	clang-driver.cpp
clang-driver.cpp: ${LLVM_SRCDIR}/cmake/modules/llvm-driver-template.cpp.in
	${TOOL_SED} -e 's/@TOOL_NAME@/clang/g' < ${.ALLSRC} > ${.TARGET}

# LLVM 22 reconciled link library set (from CMake clang link order,
# reachover names). Wrapped in --start-group by link.mk for ordering.
CLANG_LIBS= \
	clangBasic \
	clangCodeGen \
	clangCodeGenTargetBuiltins \
	clangDriver \
	clangFrontend \
	clangFrontendTool \
	clangOptions \
	clangSerialization \
	clangExtractAPI \
	clangInstallAPI \
	clangFrontendRewrite \
	clangStaticAnalyzerFrontend \
	clangStaticAnalyzerCheckers \
	clangStaticAnalyzerCore \
	clangCrossTU \
	clangIndex \
	clangParse \
	clangSema \
	clangAPINotes \
	clangEdit \
	clangAnalysis \
	clangASTMatchers \
	clangAST \
	clangSupport \
	clangFormat \
	clangToolingInclusions \
	clangToolingCore \
	clangRewrite \
	clangLex

LLVM_LIBS= \
	X86CodeGen \
	X86AsmParser \
	X86MCTargetDesc \
	X86Disassembler \
	X86TargetInfo \
	PowerPCCodeGen \
	PowerPCAsmParser \
	PowerPCMCTargetDesc \
	PowerPCDisassembler \
	PowerPCTargetInfo \
	SparcCodeGen \
	SparcAsmParser \
	SparcMCTargetDesc \
	SparcDisassembler \
	SparcTargetInfo \
	AArch64CodeGen \
	AArch64AsmParser \
	AArch64MCTargetDesc \
	AArch64Disassembler \
	AArch64TargetInfo \
	AArch64Utils \
	ARMCodeGen \
	ARMAsmParser \
	ARMMCTargetDesc \
	ARMDisassembler \
	ARMTargetInfo \
	ARMUtils \
	MipsCodeGen \
	MipsAsmParser \
	MipsMCTargetDesc \
	MipsDisassembler \
	MipsTargetInfo \
	Analysis \
	CodeGen \
	IR \
	ipo \
	TransformsAggressiveInstCombine \
	InstCombine \
	Instrumentation \
	MC \
	MCParser \
	ObjCARC \
	Option \
	ScalarOpts \
	Support \
	TargetParser \
	TransformsUtils \
	Vectorize \
	AsmPrinter \
	MCDisassembler \
	ProfileDataCoverage \
	FrontendDriver \
	LTO \
	Passes \
	TransformsCFGuard \
	GlobalISel \
	SelectionDAG \
	CodeGenTypes \
	IRPrinter \
	Target \
	HipStdPar \
	TransformsCoroutines \
	SandboxIR \
	CGData \
	BitWriter \
	Linker \
	Extensions \
	TextAPI \
	WindowsDriver \
	Plugins \
	FrontendOpenMP \
	FrontendOffloading \
	FrontendAtomic \
	ProfileData \
	DebugInfoDWARF \
	DebugInfoCodeView \
	DebugInfoMSF \
	DebugInfoBTF \
	Object \
	IRReader \
	BitReader \
	AsmParser \
	FrontendHLSL \
	BinaryFormat \
	Remarks \
	BitstreamReader \
	FrontendDirective \
	Demangle

# AMDGPU backend + MIRParser are target-only: the host cross-clang's config
# Targets.def lists only the 6 non-AMDGPU backends, and the host lib build
# gates these with !HOSTLIB. (The full target clang links them.)
.if !defined(HOSTPROG)
LLVM_LIBS+= \
	AMDGPUCodeGen \
	AMDGPUAsmParser \
	AMDGPUMCTargetDesc \
	AMDGPUDisassembler \
	AMDGPUTargetInfo \
	AMDGPUUtils \
	MIRParser \
	DebugInfoSymbolize \
	DebugInfoGSYM \
	DebugInfoPDB \
	ObjectYAML
.endif

.include "${.PARSEDIR}/../../link.mk"

LDADD+=	-lz
.if !defined(HOSTPROG)
DPADD+=	${LIBZ}
.endif

.include <bsd.prog.mk>
