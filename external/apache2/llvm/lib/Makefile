# $NetBSD: Makefile,v 1.4 2021/05/30 01:56:50 joerg Exp $

.include <bsd.own.mk>

SUBDIR=	\
	libLLVMAnalysis \
	libLLVMAsmParser \
	libLLVMAsmPrinter \
	libLLVMBinaryFormat \
	libLLVMBitReader \
	libLLVMBitWriter \
	libLLVMBitstreamReader \
	libLLVMCodeGen \
	libLLVMDebugInfoCodeView \
	libLLVMDebugInfoDWARF \
	libLLVMDebugInfoMSF \
	libLLVMDemangle \
	libLLVMFrontendOpenMP \
	libLLVMGlobalISel \
	libLLVMIR \
	libLLVMIRReader \
	libLLVMInstCombine \
	libLLVMInstrumentation \
	libLLVMipo \
	libLLVMLTO \
	libLLVMLinker \
	libLLVMMC \
	libLLVMMCDisassembler \
	libLLVMMCParser \
	libLLVMObjCARC \
	libLLVMObject \
	libLLVMOption \
	libLLVMPasses \
	libLLVMProfileData \
	libLLVMProfileDataCoverage \
	libLLVMRemarks \
	libLLVMScalarOpts \
	libLLVMSelectionDAG \
	libLLVMSupport \
	libLLVMTableGen \
	libLLVMTarget \
	libLLVMTextAPI \
	libLLVMTransformsAggressiveInstCombine \
	libLLVMTransformsCFGuard \
	libLLVMTransformsCoroutines \
	libLLVMTransformsUtils \
	libLLVMVectorize \
	libLLVMAArch64CodeGen \
	libLLVMARMCodeGen \
	libLLVMMipsCodeGen \
	libLLVMPowerPCCodeGen \
	libLLVMSparcCodeGen \
	libLLVMX86CodeGen

.if !defined(HOSTLIB) && !defined(_BSD_HOSTINIT_MK_)
SUBDIR+= \
	libLLVMAMDGPUCodeGen
.endif

SUBDIR+= .WAIT

.if !defined(HOSTLIB) && !defined(_BSD_HOSTINIT_MK_)
SUBDIR+= \
	libLLVMMIRParser \
	libLLVMAMDGPUMCTargetDesc \
	libLLVMAMDGPUTargetInfo \
	libLLVMAMDGPUAsmParser \
	libLLVMAMDGPUUtils
.endif

SUBDIR+= \
	libLLVMAArch64AsmParser \
	libLLVMAArch64Disassembler \
	libLLVMAArch64MCTargetDesc \
	libLLVMAArch64TargetInfo \
	libLLVMAArch64Utils

SUBDIR+= \
	libLLVMARMAsmParser \
	libLLVMARMDisassembler \
	libLLVMARMMCTargetDesc \
	libLLVMARMTargetInfo \
	libLLVMARMUtils

SUBDIR+= \
	libLLVMMipsAsmParser \
	libLLVMMipsDisassembler \
	libLLVMMipsMCTargetDesc \
	libLLVMMipsTargetInfo

SUBDIR+= \
	libLLVMPowerPCAsmParser \
	libLLVMPowerPCDisassembler \
	libLLVMPowerPCMCTargetDesc \
	libLLVMPowerPCTargetInfo

SUBDIR+= \
	libLLVMSparcAsmParser \
	libLLVMSparcDisassembler \
	libLLVMSparcMCTargetDesc \
	libLLVMSparcTargetInfo

SUBDIR+= \
	libLLVMX86AsmParser \
	libLLVMX86Disassembler \
	libLLVMX86MCTargetDesc \
	libLLVMX86TargetInfo

.if ${NO_LLVM_DEVELOPER:Uno} == "no" && ${LLVM_DEVELOPER:U} == "yes"
SUBDIR+= \
	libLLVMAMDGPUDisassembler \
	libLLVMExecutionEngine \
	libLLVMExecutionEngineJITLink \
	libLLVMExecutionEngineOrcShared \
	libLLVMExecutionEngineOrcTargetProcess \
	libLLVMFileCheck \
	libLLVMInterfaceStub \
	libLLVMMCJIT \
	libLLVMOrc \
	libLLVMRuntimeDyld
.endif

.if !defined(HOSTLIB) && !defined(_BSD_HOSTINIT_MK_)
SUBDIR+= \
	libLLVMDebugInfoPDB \
	libLLVMDebugInfoSymbolize
.endif

# clang 22's FrontendOffloading uses ELFYAML (yaml2elf), so the host clang
# needs ObjectYAML built as well.
SUBDIR+= \
	libLLVMObjectYAML

.if ${NO_LLVM_DEVELOPER:Uno} == "no" && ${LLVM_DEVELOPER:U} == "yes"
SUBDIR+= \
	libLLVMDebugInfoGSYM \
	libLLVMHello \
	libLLVMInterpreter \
	libLLVMLineEditor \
	libLLVMToolDrivers \
	libLLVMToolDriversDlltool \
	libLLVMXRay
.endif

SUBDIR+= \
	libclangAnalysis \
	libclangAST \
	libclangASTMatchers \
	libclangBasic \
	libclangCodeGen \
	libclangCodeGenTargetBuiltins \
	libclangCrossTU \
	libclangDriver \
	libclangEdit \
	libclangFrontend \
	libclangFrontendRewrite \
	libclangFrontendTool \
	libclangIndex \
	libclangLex \
	libclangParse \
	libclangRewrite \
	libclangSema \
	libclangSerialization \
	libclangStaticAnalyzerCheckers \
	libclangStaticAnalyzerCore \
	libclangToolingCore \
	.WAIT \
	libclangStaticAnalyzerFrontend

.if ${NO_LLVM_DEVELOPER:Uno} == "no" && ${LLVM_DEVELOPER:U} == "yes"
SUBDIR+= \
	libclangASTMatchersDynamic \
	libclangIndexSerialization \
	libclangInterpreter \
	libclangTesting \
	libclangTooling \
	libclangToolingDependencyScanning \
	libclangToolingRefactoring \
	libclangToolingSyntax
.endif

# clang 22 links these as core libraries (were developer-only in 13).
SUBDIR+= \
	libclangAPINotes \
	libclangFormat \
	libclangToolingInclusions

# LLVM 22 new libraries
SUBDIR+=	libclang \
	libclangDependencyScanning \
	libclangDirectoryWatcher \
	libclangExtractAPI \
	libclangHandleCXX \
	libclangHandleLLVM \
	libclangInstallAPI \
	libclangOptions \
	libclangSupport \
	libLLVMABI \
	libLLVMCAS \
	libLLVMCFIVerify \
	libLLVMCGData \
	libLLVMCodeGenTypes \
	libLLVMDebugInfoBTF \
	libLLVMDebuginfod \
	libLLVMDebugInfoLogicalView \
	libLLVMDiff \
	libLLVMDTLTO \
	libLLVMDWARFCFIChecker \
	libLLVMDWP \
	libLLVMExegesis \
	libLLVMExegesisAArch64 \
	libLLVMExegesisMips \
	libLLVMExegesisPowerPC \
	libLLVMExegesisX86 \
	libLLVMExtensions \
	libLLVMFrontendAtomic \
	libLLVMFrontendDirective \
	libLLVMFrontendDriver \
	libLLVMFrontendHLSL \
	libLLVMFrontendOffloading \
	libLLVMFuzzerCLI \
	libLLVMFuzzMutate \
	libLLVMHipStdPar \
	libLLVMIRPrinter \
	libLLVMObjCopy \
	libLLVMOptDriver \
	libLLVMPlugins \
	libLLVMSandboxIR \
	libLLVMTableGenBasic \
	libLLVMTableGenCommon \
	libLLVMTargetParser \
	libLLVMTelemetry \
	libLLVMWindowsDriver \
	libLLVMWindowsManifest

.include <bsd.subdir.mk>
