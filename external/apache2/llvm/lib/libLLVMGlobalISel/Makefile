#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:52 joerg Exp $

LIB=	LLVMGlobalISel

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/lib/CodeGen/GlobalISel

SRCS+=	CSEInfo.cpp \
	CSEMIRBuilder.cpp \
	CallLowering.cpp \
	Combiner.cpp \
	CombinerHelper.cpp \
	CombinerHelperArtifacts.cpp \
	CombinerHelperCasts.cpp \
	CombinerHelperCompares.cpp \
	CombinerHelperVectorOps.cpp \
	GIMatchTableExecutor.cpp \
	GISelChangeObserver.cpp \
	GISelValueTracking.cpp \
	GlobalISel.cpp \
	IRTranslator.cpp \
	InlineAsmLowering.cpp \
	InstructionSelect.cpp \
	InstructionSelector.cpp \
	LegacyLegalizerInfo.cpp \
	LegalityPredicates.cpp \
	LegalizeMutations.cpp \
	Legalizer.cpp \
	LegalizerHelper.cpp \
	LegalizerInfo.cpp \
	LoadStoreOpt.cpp \
	Localizer.cpp \
	LostDebugLocObserver.cpp \
	MachineFloatingPointPredicateUtils.cpp \
	MachineIRBuilder.cpp \
	RegBankSelect.cpp \
	Utils.cpp

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
