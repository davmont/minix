#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:56 joerg Exp $

LIB=	clangAnalysis

.include <bsd.init.mk>

.PATH: ${CLANG_SRCDIR}/lib/Analysis

SRCS+=	AnalysisDeclContext.cpp \
	BodyFarm.cpp \
	CFG.cpp \
	CFGReachabilityAnalysis.cpp \
	CFGStmtMap.cpp \
	CallGraph.cpp \
	CalledOnceCheck.cpp \
	CloneDetection.cpp \
	CocoaConventions.cpp \
	CodeInjector.cpp \
	ConstructionContext.cpp \
	Consumed.cpp \
	Dominators.cpp \
	ExprMutationAnalyzer.cpp \
	FixitUtil.cpp \
	FlowSensitive/ASTOps.cpp \
	FlowSensitive/AdornedCFG.cpp \
	FlowSensitive/Arena.cpp \
	FlowSensitive/CNFFormula.cpp \
	FlowSensitive/DataflowAnalysisContext.cpp \
	FlowSensitive/DataflowEnvironment.cpp \
	FlowSensitive/DebugSupport.cpp \
	FlowSensitive/Formula.cpp \
	FlowSensitive/FormulaSerialization.cpp \
	FlowSensitive/HTMLLogger.cpp \
	FlowSensitive/Logger.cpp \
	FlowSensitive/Models/ChromiumCheckModel.cpp \
	FlowSensitive/Models/UncheckedOptionalAccessModel.cpp \
	FlowSensitive/Models/UncheckedStatusOrAccessModel.cpp \
	FlowSensitive/RecordOps.cpp \
	FlowSensitive/SimplifyConstraints.cpp \
	FlowSensitive/SmartPointerAccessorCaching.cpp \
	FlowSensitive/Transfer.cpp \
	FlowSensitive/TypeErasedDataflowAnalysis.cpp \
	FlowSensitive/Value.cpp \
	FlowSensitive/WatchedLiteralsSolver.cpp \
	IntervalPartition.cpp \
	IssueHash.cpp \
	LifetimeSafety/Checker.cpp \
	LifetimeSafety/Facts.cpp \
	LifetimeSafety/FactsGenerator.cpp \
	LifetimeSafety/LifetimeAnnotations.cpp \
	LifetimeSafety/LifetimeSafety.cpp \
	LifetimeSafety/LifetimeStats.cpp \
	LifetimeSafety/LiveOrigins.cpp \
	LifetimeSafety/LoanPropagation.cpp \
	LifetimeSafety/Loans.cpp \
	LifetimeSafety/Origins.cpp \
	LiveVariables.cpp \
	MacroExpansionContext.cpp \
	ObjCNoReturn.cpp \
	PathDiagnostic.cpp \
	PostOrderCFGView.cpp \
	ProgramPoint.cpp \
	ReachableCode.cpp \
	RetainSummaryManager.cpp \
	Scalable/ASTEntityMapping.cpp \
	Scalable/Model/BuildNamespace.cpp \
	Scalable/Model/EntityIdTable.cpp \
	Scalable/Model/EntityName.cpp \
	ThreadSafety.cpp \
	ThreadSafetyCommon.cpp \
	ThreadSafetyLogical.cpp \
	ThreadSafetyTIL.cpp \
	UninitializedValues.cpp \
	UnsafeBufferUsage.cpp \
	plugins/CheckerDependencyHandling/CheckerDependencyHandling.cpp \
	plugins/CheckerOptionHandling/CheckerOptionHandling.cpp \
	plugins/SampleAnalyzer/MainCallChecker.cpp

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
