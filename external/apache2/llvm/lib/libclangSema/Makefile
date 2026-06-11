#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:57 joerg Exp $

LIB=	clangSema

.include <bsd.init.mk>

.PATH: ${CLANG_SRCDIR}/lib/Sema

SRCS+=	AnalysisBasedWarnings.cpp \
	CheckExprLifetime.cpp \
	CodeCompleteConsumer.cpp \
	DeclSpec.cpp \
	DelayedDiagnostic.cpp \
	HLSLBuiltinTypeDeclBuilder.cpp \
	HLSLExternalSemaSource.cpp \
	HeuristicResolver.cpp \
	IdentifierResolver.cpp \
	JumpDiagnostics.cpp \
	MultiplexExternalSemaSource.cpp \
	ParsedAttr.cpp \
	Scope.cpp \
	ScopeInfo.cpp \
	Sema.cpp \
	SemaAMDGPU.cpp \
	SemaAPINotes.cpp \
	SemaARM.cpp \
	SemaAVR.cpp \
	SemaAccess.cpp \
	SemaAttr.cpp \
	SemaAvailability.cpp \
	SemaBPF.cpp \
	SemaBase.cpp \
	SemaBoundsSafety.cpp \
	SemaCUDA.cpp \
	SemaCXXScopeSpec.cpp \
	SemaCast.cpp \
	SemaChecking.cpp \
	SemaCodeComplete.cpp \
	SemaConcept.cpp \
	SemaConsumer.cpp \
	SemaCoroutine.cpp \
	SemaDecl.cpp \
	SemaDeclAttr.cpp \
	SemaDeclCXX.cpp \
	SemaDeclObjC.cpp \
	SemaDirectX.cpp \
	SemaExceptionSpec.cpp \
	SemaExpr.cpp \
	SemaExprCXX.cpp \
	SemaExprMember.cpp \
	SemaExprObjC.cpp \
	SemaFixItUtils.cpp \
	SemaFunctionEffects.cpp \
	SemaHLSL.cpp \
	SemaHexagon.cpp \
	SemaInit.cpp \
	SemaLambda.cpp \
	SemaLookup.cpp \
	SemaLoongArch.cpp \
	SemaM68k.cpp \
	SemaMIPS.cpp \
	SemaMSP430.cpp \
	SemaModule.cpp \
	SemaNVPTX.cpp \
	SemaObjC.cpp \
	SemaObjCProperty.cpp \
	SemaOpenACC.cpp \
	SemaOpenACCAtomic.cpp \
	SemaOpenACCClause.cpp \
	SemaOpenACCClauseAppertainment.cpp \
	SemaOpenCL.cpp \
	SemaOpenMP.cpp \
	SemaOverload.cpp \
	SemaPPC.cpp \
	SemaPseudoObject.cpp \
	SemaRISCV.cpp \
	SemaSPIRV.cpp \
	SemaSYCL.cpp \
	SemaStmt.cpp \
	SemaStmtAsm.cpp \
	SemaStmtAttr.cpp \
	SemaSwift.cpp \
	SemaSystemZ.cpp \
	SemaTemplate.cpp \
	SemaTemplateDeduction.cpp \
	SemaTemplateDeductionGuide.cpp \
	SemaTemplateInstantiate.cpp \
	SemaTemplateInstantiateDecl.cpp \
	SemaTemplateVariadic.cpp \
	SemaType.cpp \
	SemaTypeTraits.cpp \
	SemaWasm.cpp \
	SemaX86.cpp \
	TypeLocBuilder.cpp

CLANG_TABLEGEN_SRC=		OpenCLBuiltins.td
CLANG_TABLEGEN_OUTPUT= \
	OpenCLBuiltins.inc|-gen-clang-opencl-builtins

.include "${.PARSEDIR}/../../tablegen.mk"

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
