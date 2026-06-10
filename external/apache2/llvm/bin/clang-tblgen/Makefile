#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:46 joerg Exp $

PROG_CXX=	clang-tblgen
NOMAN=		yes

.include <bsd.init.mk>

.PATH: ${CLANG_SRCDIR}/utils/TableGen
# RISCVVIntrinsicUtils.cpp moved to clang/lib/Support in LLVM 22 (was in utils/TableGen);
# clang-tblgen still references clang::RISCV::* so pull it in directly.
.PATH: ${CLANG_SRCDIR}/lib/Support

SRCS=	ASTTableGen.cpp \
	CIRLoweringEmitter.cpp \
	ClangASTNodesEmitter.cpp \
	ClangASTPropertiesEmitter.cpp \
	ClangAttrEmitter.cpp \
	ClangBuiltinsEmitter.cpp \
	ClangBuiltinTemplatesEmitter.cpp \
	ClangCommentCommandInfoEmitter.cpp \
	ClangCommentHTMLNamedCharacterReferenceEmitter.cpp \
	ClangCommentHTMLTagsEmitter.cpp \
	ClangDataCollectorsEmitter.cpp \
	ClangDiagnosticsEmitter.cpp \
	ClangOpcodesEmitter.cpp \
	ClangOpenCLBuiltinEmitter.cpp \
	ClangOptionDocEmitter.cpp \
	ClangSACheckersEmitter.cpp \
	ClangSyntaxEmitter.cpp \
	ClangTypeNodesEmitter.cpp \
	MveEmitter.cpp \
	NeonEmitter.cpp \
	RISCVVEmitter.cpp \
	RISCVVIntrinsicUtils.cpp \
	SveEmitter.cpp \
	TableGen.cpp

LLVM_LIBS+= \
	TableGen \
	Support \
	Demangle

LLVM_LIBS+=	TargetParser

.include "${.PARSEDIR}/../../link.mk"

.include <bsd.prog.mk>
