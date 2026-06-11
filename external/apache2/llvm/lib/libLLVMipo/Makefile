#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:55 joerg Exp $

LIB=	LLVMipo

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/lib/Transforms/IPO

SRCS+=	AlwaysInliner.cpp \
	Annotation2Metadata.cpp \
	ArgumentPromotion.cpp \
	Attributor.cpp \
	AttributorAttributes.cpp \
	BarrierNoopPass.cpp \
	BlockExtractor.cpp \
	CalledValuePropagation.cpp \
	ConstantMerge.cpp \
	CrossDSOCFI.cpp \
	DeadArgumentElimination.cpp \
	ElimAvailExtern.cpp \
	EmbedBitcodePass.cpp \
	ExpandVariadics.cpp \
	ExtractGV.cpp \
	FatLTOCleanup.cpp \
	ForceFunctionAttrs.cpp \
	FunctionAttrs.cpp \
	FunctionImport.cpp \
	FunctionSpecialization.cpp \
	GlobalDCE.cpp \
	GlobalOpt.cpp \
	GlobalSplit.cpp \
	HotColdSplitting.cpp \
	IPO.cpp \
	IROutliner.cpp \
	InferFunctionAttrs.cpp \
	Inliner.cpp \
	Internalize.cpp \
	LoopExtractor.cpp \
	LowerTypeTests.cpp \
	MemProfContextDisambiguation.cpp \
	MergeFunctions.cpp \
	ModuleInliner.cpp \
	OpenMPOpt.cpp \
	PartialInlining.cpp \
	SCCP.cpp \
	SampleContextTracker.cpp \
	SampleProfile.cpp \
	SampleProfileMatcher.cpp \
	SampleProfileProbe.cpp \
	StripDeadPrototypes.cpp \
	StripSymbols.cpp \
	ThinLTOBitcodeWriter.cpp \
	WholeProgramDevirt.cpp

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
