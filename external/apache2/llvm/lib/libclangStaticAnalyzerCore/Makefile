#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:57 joerg Exp $

LIB=	clangStaticAnalyzerCore

.include <bsd.init.mk>

.PATH: ${CLANG_SRCDIR}/lib/StaticAnalyzer/Core

SRCS+=	APSIntType.cpp \
	AnalysisManager.cpp \
	AnalyzerOptions.cpp \
	BasicValueFactory.cpp \
	BlockCounter.cpp \
	BugReporter.cpp \
	BugReporterVisitors.cpp \
	BugSuppression.cpp \
	CallDescription.cpp \
	CallEvent.cpp \
	Checker.cpp \
	CheckerContext.cpp \
	CheckerHelpers.cpp \
	CheckerManager.cpp \
	CheckerRegistryData.cpp \
	CommonBugCategories.cpp \
	ConstraintManager.cpp \
	CoreEngine.cpp \
	DynamicExtent.cpp \
	DynamicType.cpp \
	EntryPointStats.cpp \
	Environment.cpp \
	ExplodedGraph.cpp \
	ExprEngine.cpp \
	ExprEngineC.cpp \
	ExprEngineCXX.cpp \
	ExprEngineCallAndReturn.cpp \
	ExprEngineObjC.cpp \
	FunctionSummary.cpp \
	HTMLDiagnostics.cpp \
	LoopUnrolling.cpp \
	LoopWidening.cpp \
	MemRegion.cpp \
	PlistDiagnostics.cpp \
	ProgramState.cpp \
	RangeConstraintManager.cpp \
	RangedConstraintManager.cpp \
	RegionStore.cpp \
	SMTConstraintManager.cpp \
	SValBuilder.cpp \
	SVals.cpp \
	SarifDiagnostics.cpp \
	SimpleConstraintManager.cpp \
	SimpleSValBuilder.cpp \
	Store.cpp \
	SymbolManager.cpp \
	TextDiagnostics.cpp \
	WorkList.cpp \
	Z3CrosscheckVisitor.cpp

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
