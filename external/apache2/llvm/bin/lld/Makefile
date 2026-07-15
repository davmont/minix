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

# AMDGPU backend is target-only, exactly as in clang's reachover: the host
# cross-clang's config Targets.def lists only the 6 non-AMDGPU backends, so the
# host ld.lld's InitializeAllTargets() never references AMDGPU and must not link
# it (the host lib build excludes it).  The target ld.lld's Targets.def DOES list
# AMDGPU, so lld/ELF/Driver.cpp's InitializeAllTargets() references
# LLVMInitializeAMDGPUTargetInfo & co., which only resolve if these are linked.
# Without this, a full (MKLLVMCMDS=yes) build of the shipped ld.lld fails with
# "undefined symbol: LLVMInitializeAMDGPUTargetInfo".
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
	DebugInfoPDB
.endif

.include "${.PARSEDIR}/../../link.mk"

LDADD+=	-lz
# For a host build, ${LIBZ} would name the *host's* /usr/lib/libz.a, which is
# not ours to depend on.  clang guards it the same way.
.if !defined(HOSTPROG)
DPADD+=	${LIBZ}
.endif

.include <bsd.prog.mk>
