#	MINIX 3: ld.lld, the LLVM ELF linker.

PROG_CXX=	ld.lld
NOMAN=		yes

.include <bsd.init.mk>

.PATH: ${.CURDIR}

SRCS=	ld.lld.cpp

LLD_LIBS+= \
	ELF \
	Common

# The same LLVM component set clang links.  lld/ELF's own CMake asks for
# ${LLVM_TARGETS_TO_BUILD} plus LTO, Passes, Object, MC and friends; rather than
# re-derive that, reuse the set clang already proves works.  Static linking pulls
# in only what is referenced, and link.mk wraps the lot in a --start-group, so
# neither the extra names nor their order costs us anything.
LLVM_LIBS+= \
	DTLTO \
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
	ObjectYAML \
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

# The *target* LLVM is configured with the amdgpu backend (Makefile.inc adds it
# to LLVM_TARGETS in the non-host branch), so lld/ELF's InitializeAllTargets
# references LLVMInitializeAMDGPU*.  The host LLVM is not, and the host llvm-lib
# never builds these archives, so pull them in only for the target build --
# otherwise the host ld.lld fails with "don't know how to make
# libLLVMAMDGPUCodeGen.a".
.if !defined(HOSTPROG)
LLVM_LIBS+= \
	AMDGPUCodeGen \
	AMDGPUTargetInfo \
	AMDGPUMCTargetDesc \
	AMDGPUAsmParser \
	AMDGPUUtils \
	MIRParser
.endif

.include "${.PARSEDIR}/../../link.mk"

LDADD+=	-lz
# For a host build, ${LIBZ} would name the *host's* /usr/lib/libz.a, which is
# not ours to depend on.  clang guards it the same way.
.if !defined(HOSTPROG)
DPADD+=	${LIBZ}
.endif

.include <bsd.prog.mk>
