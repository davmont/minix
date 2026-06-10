#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:54 joerg Exp $

LIB=	LLVMScalarOpts

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/lib/Transforms/Scalar

SRCS+=	ADCE.cpp \
	AlignmentFromAssumptions.cpp \
	AnnotationRemarks.cpp \
	BDCE.cpp \
	CallSiteSplitting.cpp \
	ConstantHoisting.cpp \
	ConstraintElimination.cpp \
	CorrelatedValuePropagation.cpp \
	DCE.cpp \
	DFAJumpThreading.cpp \
	DeadStoreElimination.cpp \
	DivRemPairs.cpp \
	DropUnnecessaryAssumes.cpp \
	EarlyCSE.cpp \
	FlattenCFGPass.cpp \
	Float2Int.cpp \
	GVN.cpp \
	GVNHoist.cpp \
	GVNSink.cpp \
	GuardWidening.cpp \
	IVUsersPrinter.cpp \
	IndVarSimplify.cpp \
	InductiveRangeCheckElimination.cpp \
	InferAddressSpaces.cpp \
	InferAlignment.cpp \
	InstSimplifyPass.cpp \
	JumpTableToSwitch.cpp \
	JumpThreading.cpp \
	LICM.cpp \
	LoopAccessAnalysisPrinter.cpp \
	LoopBoundSplit.cpp \
	LoopDataPrefetch.cpp \
	LoopDeletion.cpp \
	LoopDistribute.cpp \
	LoopFlatten.cpp \
	LoopFuse.cpp \
	LoopIdiomRecognize.cpp \
	LoopInstSimplify.cpp \
	LoopInterchange.cpp \
	LoopLoadElimination.cpp \
	LoopPassManager.cpp \
	LoopPredication.cpp \
	LoopRotation.cpp \
	LoopSimplifyCFG.cpp \
	LoopSink.cpp \
	LoopStrengthReduce.cpp \
	LoopTermFold.cpp \
	LoopUnrollAndJamPass.cpp \
	LoopUnrollPass.cpp \
	LoopVersioningLICM.cpp \
	LowerAtomicPass.cpp \
	LowerConstantIntrinsics.cpp \
	LowerExpectIntrinsic.cpp \
	LowerGuardIntrinsic.cpp \
	LowerMatrixIntrinsics.cpp \
	LowerWidenableCondition.cpp \
	MakeGuardsExplicit.cpp \
	MemCpyOptimizer.cpp \
	MergeICmps.cpp \
	MergedLoadStoreMotion.cpp \
	NaryReassociate.cpp \
	NewGVN.cpp \
	PartiallyInlineLibCalls.cpp \
	PlaceSafepoints.cpp \
	Reassociate.cpp \
	Reg2Mem.cpp \
	RewriteStatepointsForGC.cpp \
	SCCP.cpp \
	SROA.cpp \
	Scalar.cpp \
	ScalarizeMaskedMemIntrin.cpp \
	Scalarizer.cpp \
	SeparateConstOffsetFromGEP.cpp \
	SimpleLoopUnswitch.cpp \
	SimplifyCFGPass.cpp \
	Sink.cpp \
	SpeculativeExecution.cpp \
	StraightLineStrengthReduce.cpp \
	StructurizeCFG.cpp \
	TailRecursionElimination.cpp \
	WarnMissedTransforms.cpp

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
