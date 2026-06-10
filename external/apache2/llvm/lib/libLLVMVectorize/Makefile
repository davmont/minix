#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:55 joerg Exp $

LIB=	LLVMVectorize

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/lib/Transforms/Vectorize

SRCS+=	LoadStoreVectorizer.cpp \
	LoopIdiomVectorize.cpp \
	LoopVectorizationLegality.cpp \
	LoopVectorize.cpp \
	SLPVectorizer.cpp \
	SandboxVectorizer/DependencyGraph.cpp \
	SandboxVectorizer/InstrMaps.cpp \
	SandboxVectorizer/Interval.cpp \
	SandboxVectorizer/Legality.cpp \
	SandboxVectorizer/Passes/BottomUpVec.cpp \
	SandboxVectorizer/Passes/PackReuse.cpp \
	SandboxVectorizer/Passes/RegionsFromBBs.cpp \
	SandboxVectorizer/Passes/RegionsFromMetadata.cpp \
	SandboxVectorizer/Passes/SeedCollection.cpp \
	SandboxVectorizer/Passes/TransactionAcceptOrRevert.cpp \
	SandboxVectorizer/Passes/TransactionSave.cpp \
	SandboxVectorizer/SandboxVectorizer.cpp \
	SandboxVectorizer/SandboxVectorizerPassBuilder.cpp \
	SandboxVectorizer/Scheduler.cpp \
	SandboxVectorizer/SeedCollector.cpp \
	SandboxVectorizer/VecUtils.cpp \
	VPlan.cpp \
	VPlanAnalysis.cpp \
	VPlanConstruction.cpp \
	VPlanPredicator.cpp \
	VPlanRecipes.cpp \
	VPlanSLP.cpp \
	VPlanTransforms.cpp \
	VPlanUnroll.cpp \
	VPlanUtils.cpp \
	VPlanVerifier.cpp \
	VectorCombine.cpp \
	Vectorize.cpp

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
