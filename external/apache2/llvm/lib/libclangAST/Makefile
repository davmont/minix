#	$NetBSD: Makefile,v 1.4 2021/05/30 01:56:55 joerg Exp $

LIB=	clangAST

.include <bsd.init.mk>

.PATH: ${CLANG_SRCDIR}/lib/AST
.PATH: ${CLANG_SRCDIR}/lib/AST/ByteCode

SRCS+=	APValue.cpp \
	ASTConcept.cpp \
	ASTConsumer.cpp \
	ASTContext.cpp \
	ASTDiagnostic.cpp \
	ASTDumper.cpp \
	ASTImporter.cpp \
	ASTImporterLookupTable.cpp \
	ASTStructuralEquivalence.cpp \
	ASTTypeTraits.cpp \
	AttrDocTable.cpp \
	AttrImpl.cpp \
	Availability.cpp \
	BitcastBuffer.cpp \
	ByteCodeEmitter.cpp \
	CXXInheritance.cpp \
	Comment.cpp \
	CommentBriefParser.cpp \
	CommentCommandTraits.cpp \
	CommentLexer.cpp \
	CommentParser.cpp \
	CommentSema.cpp \
	ComparisonCategories.cpp \
	Compiler.cpp \
	ComputeDependence.cpp \
	Context.cpp \
	DataCollection.cpp \
	Decl.cpp \
	DeclBase.cpp \
	DeclCXX.cpp \
	DeclFriend.cpp \
	DeclGroup.cpp \
	DeclObjC.cpp \
	DeclOpenACC.cpp \
	DeclOpenMP.cpp \
	DeclPrinter.cpp \
	DeclTemplate.cpp \
	DeclarationName.cpp \
	Descriptor.cpp \
	Disasm.cpp \
	DynamicAllocator.cpp \
	DynamicRecursiveASTVisitor.cpp \
	EvalEmitter.cpp \
	EvaluationResult.cpp \
	Expr.cpp \
	ExprCXX.cpp \
	ExprClassification.cpp \
	ExprConcepts.cpp \
	ExprConstant.cpp \
	ExprObjC.cpp \
	ExternalASTMerger.cpp \
	ExternalASTSource.cpp \
	Floating.cpp \
	FormatString.cpp \
	Function.cpp \
	FunctionPointer.cpp \
	InferAlloc.cpp \
	InheritViz.cpp \
	InitMap.cpp \
	Interp.cpp \
	InterpBlock.cpp \
	InterpBuiltin.cpp \
	InterpBuiltinBitCast.cpp \
	InterpFrame.cpp \
	InterpShared.cpp \
	InterpStack.cpp \
	InterpState.cpp \
	ItaniumCXXABI.cpp \
	ItaniumMangle.cpp \
	JSONNodeDumper.cpp \
	Mangle.cpp \
	MemberPointer.cpp \
	MicrosoftCXXABI.cpp \
	MicrosoftMangle.cpp \
	NSAPI.cpp \
	NestedNameSpecifier.cpp \
	ODRDiagsEmitter.cpp \
	ODRHash.cpp \
	OSLog.cpp \
	OpenACCClause.cpp \
	OpenMPClause.cpp \
	ParentMap.cpp \
	ParentMapContext.cpp \
	Pointer.cpp \
	PrimType.cpp \
	PrintfFormatString.cpp \
	Program.cpp \
	QualTypeNames.cpp \
	Randstruct.cpp \
	RawCommentList.cpp \
	Record.cpp \
	RecordLayout.cpp \
	RecordLayoutBuilder.cpp \
	ScanfFormatString.cpp \
	SelectorLocationsKind.cpp \
	Source.cpp \
	State.cpp \
	Stmt.cpp \
	StmtCXX.cpp \
	StmtIterator.cpp \
	StmtObjC.cpp \
	StmtOpenACC.cpp \
	StmtOpenMP.cpp \
	StmtPrinter.cpp \
	StmtProfile.cpp \
	StmtViz.cpp \
	TemplateBase.cpp \
	TemplateName.cpp \
	TextNodeDumper.cpp \
	Type.cpp \
	TypeLoc.cpp \
	TypePrinter.cpp \
	VTTBuilder.cpp \
	VTableBuilder.cpp



CLANG_TABLEGEN_SRC=	Opcodes.td
CLANG_TABLEGEN_OUTPUT= \
	Opcodes.inc|-gen-clang-opcodes

.include "${.PARSEDIR}/../../tablegen.mk"

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
