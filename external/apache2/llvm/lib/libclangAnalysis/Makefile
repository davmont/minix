#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:56 joerg Exp $

LIB=	clangAnalysis

.include <bsd.init.mk>

.PATH: ${CLANG_SRCDIR}/lib/Analysis
.PATH: ${CLANG_SRCDIR}/lib/Analysis/FlowSensitive
.PATH: ${CLANG_SRCDIR}/lib/Analysis/FlowSensitive/Models
.PATH: ${CLANG_SRCDIR}/lib/Analysis/LifetimeSafety
.PATH: ${CLANG_SRCDIR}/lib/Analysis/Scalable
.PATH: ${CLANG_SRCDIR}/lib/Analysis/Scalable/Model
.PATH: ${CLANG_SRCDIR}/lib/Analysis/plugins/CheckerDependencyHandling
.PATH: ${CLANG_SRCDIR}/lib/Analysis/plugins/CheckerOptionHandling
.PATH: ${CLANG_SRCDIR}/lib/Analysis/plugins/SampleAnalyzer

SRCS+=	ASTEntityMapping.cpp \
	ASTOps.cpp \
	AdornedCFG.cpp \
	AnalysisDeclContext.cpp \
	Arena.cpp \
	BodyFarm.cpp \
	BuildNamespace.cpp \
	CFG.cpp \
	CFGReachabilityAnalysis.cpp \
	CFGStmtMap.cpp \
	CNFFormula.cpp \
	CallGraph.cpp \
	CalledOnceCheck.cpp \
	Checker.cpp \
	CheckerDependencyHandling.cpp \
	CheckerOptionHandling.cpp \
	ChromiumCheckModel.cpp \
	CloneDetection.cpp \
	CocoaConventions.cpp \
	CodeInjector.cpp \
	ConstructionContext.cpp \
	Consumed.cpp \
	DataflowAnalysisContext.cpp \
	DataflowEnvironment.cpp \
	DebugSupport.cpp \
	Dominators.cpp \
	EntityIdTable.cpp \
	EntityName.cpp \
	ExprMutationAnalyzer.cpp \
	Facts.cpp \
	FactsGenerator.cpp \
	FixitUtil.cpp \
	Formula.cpp \
	FormulaSerialization.cpp \
	HTMLLogger.cpp \
	IntervalPartition.cpp \
	IssueHash.cpp \
	LifetimeAnnotations.cpp \
	LifetimeSafety.cpp \
	LifetimeStats.cpp \
	LiveOrigins.cpp \
	LiveVariables.cpp \
	LoanPropagation.cpp \
	Loans.cpp \
	Logger.cpp \
	MacroExpansionContext.cpp \
	MainCallChecker.cpp \
	ObjCNoReturn.cpp \
	Origins.cpp \
	PathDiagnostic.cpp \
	PostOrderCFGView.cpp \
	ProgramPoint.cpp \
	ReachableCode.cpp \
	RecordOps.cpp \
	RetainSummaryManager.cpp \
	SimplifyConstraints.cpp \
	SmartPointerAccessorCaching.cpp \
	ThreadSafety.cpp \
	ThreadSafetyCommon.cpp \
	ThreadSafetyLogical.cpp \
	ThreadSafetyTIL.cpp \
	Transfer.cpp \
	TypeErasedDataflowAnalysis.cpp \
	UncheckedOptionalAccessModel.cpp \
	UncheckedStatusOrAccessModel.cpp \
	UninitializedValues.cpp \
	UnsafeBufferUsage.cpp \
	Value.cpp \
	WatchedLiteralsSolver.cpp

# LLVM 22: HTMLLogger.cpp bundles HTML/CSS/JS into HTMLLogger.inc via the
# clang bundle_resources.py helper (a custom build step, not TableGen).
_FS=		${CLANG_SRCDIR}/lib/Analysis/FlowSensitive
DPSRCS+=	HTMLLogger.inc
CLEANFILES+=	HTMLLogger.inc
HTMLLogger.inc: ${_FS}/HTMLLogger.html ${_FS}/HTMLLogger.css ${_FS}/HTMLLogger.js
	python3 ${CLANG_SRCDIR}/utils/bundle_resources.py ${.TARGET} \
	    ${_FS}/HTMLLogger.html ${_FS}/HTMLLogger.css ${_FS}/HTMLLogger.js

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
