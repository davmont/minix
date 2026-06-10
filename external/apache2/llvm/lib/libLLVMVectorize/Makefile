#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:55 joerg Exp $

LIB=	LLVMVectorize

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/lib/Transforms/Vectorize
.PATH: ${LLVM_SRCDIR}/lib/Transforms/Vectorize/SandboxVectorizer
.PATH: ${LLVM_SRCDIR}/lib/Transforms/Vectorize/SandboxVectorizer/Passes

SRCS+=	BottomUpVec.cpp \
	DependencyGraph.cpp \
	InstrMaps.cpp \
	Interval.cpp \
	Legality.cpp \
	LoadStoreVectorizer.cpp \
	LoopIdiomVectorize.cpp \
	LoopVectorizationLegality.cpp \
	LoopVectorize.cpp \
	PackReuse.cpp \
	RegionsFromBBs.cpp \
	RegionsFromMetadata.cpp \
	SLPVectorizer.cpp \
	SandboxVectorizer.cpp \
	SandboxVectorizerPassBuilder.cpp \
	Scheduler.cpp \
	SeedCollection.cpp \
	SeedCollector.cpp \
	TransactionAcceptOrRevert.cpp \
	TransactionSave.cpp \
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
	VecUtils.cpp \
	VectorCombine.cpp \
	Vectorize.cpp

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
