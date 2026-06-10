#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:52 joerg Exp $

LIB=	LLVMIR

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/lib/IR

.include "${.PARSEDIR}/../../tablegen.mk"

SRCS+=	AbstractCallSite.cpp \
	AsmWriter.cpp \
	Assumptions.cpp \
	Attributes.cpp \
	AutoUpgrade.cpp \
	BasicBlock.cpp \
	BuiltinGCs.cpp \
	Comdat.cpp \
	ConstantFPRange.cpp \
	ConstantFold.cpp \
	ConstantRange.cpp \
	ConstantRangeList.cpp \
	Constants.cpp \
	ConvergenceVerifier.cpp \
	Core.cpp \
	CycleInfo.cpp \
	DIBuilder.cpp \
	DIExpressionOptimizer.cpp \
	DataLayout.cpp \
	DebugInfo.cpp \
	DebugInfoMetadata.cpp \
	DebugLoc.cpp \
	DebugProgramInstruction.cpp \
	DiagnosticHandler.cpp \
	DiagnosticInfo.cpp \
	DiagnosticPrinter.cpp \
	Dominators.cpp \
	DroppedVariableStats.cpp \
	DroppedVariableStatsIR.cpp \
	EHPersonalities.cpp \
	FPEnv.cpp \
	Function.cpp \
	GCStrategy.cpp \
	GVMaterializer.cpp \
	Globals.cpp \
	IRBuilder.cpp \
	IRPrintingPasses.cpp \
	InlineAsm.cpp \
	Instruction.cpp \
	Instructions.cpp \
	IntrinsicInst.cpp \
	Intrinsics.cpp \
	LLVMContext.cpp \
	LLVMContextImpl.cpp \
	LLVMRemarkStreamer.cpp \
	LegacyPassManager.cpp \
	MDBuilder.cpp \
	Mangler.cpp \
	MemoryModelRelaxationAnnotations.cpp \
	Metadata.cpp \
	Module.cpp \
	ModuleSummaryIndex.cpp \
	NVVMIntrinsicUtils.cpp \
	Operator.cpp \
	OptBisect.cpp \
	Pass.cpp \
	PassInstrumentation.cpp \
	PassManager.cpp \
	PassRegistry.cpp \
	PassTimingInfo.cpp \
	PrintPasses.cpp \
	ProfDataUtils.cpp \
	ProfileSummary.cpp \
	PseudoProbe.cpp \
	ReplaceConstant.cpp \
	RuntimeLibcalls.cpp \
	SSAContext.cpp \
	SafepointIRVerifier.cpp \
	Statepoint.cpp \
	StructuralHash.cpp \
	Type.cpp \
	TypeFinder.cpp \
	TypedPointerType.cpp \
	Use.cpp \
	User.cpp \
	VFABIDemangler.cpp \
	Value.cpp \
	ValueSymbolTable.cpp \
	VectorTypeUtils.cpp \
	Verifier.cpp

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
