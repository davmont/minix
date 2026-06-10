#	$NetBSD: Makefile,v 1.1 2019/11/11 22:44:58 joerg Exp $

PROG_CXX=	clang-format
NOMAN=		yes

.include <bsd.init.mk>

.PATH: ${CLANG_SRCDIR}/tools/clang-format

SRCS=	ClangFormat.cpp

CLANG_LIBS+= \
	clangFormat \
	clangToolingCore \
	clangToolingInclusions \
	clangTooling \
	clangFrontend \
	clangSerialization \
	clangDriver \
	clangParse \
	clangRewrite \
	clangSema \
	clangEdit \
	clangAnalysis \
	clangAST \
	clangLex \
	clangBasic

LLVM_LIBS+= \
	BitReader \
	MCParser \
	MC \
	Option \
	Support \
	Demangle

LLVM_LIBS+=	TargetParser

LLVM_LIBS+= \
	FrontendOpenMP \
	ScalarOpts \
	TransformsAggressiveInstCombine \
	InstCombine \
	FrontendOffloading \
	TransformsUtils \
	ObjectYAML \
	FrontendAtomic \
	Analysis \
	FrontendHLSL \
	ProfileData \
	DebugInfoSymbolize \
	DebugInfoGSYM \
	DebugInfoPDB \
	DebugInfoCodeView \
	DebugInfoMSF \
	DebugInfoBTF \
	DebugInfoDWARF \
	Object \
	IRReader \
	AsmParser \
	IR \
	Remarks \
	BitstreamReader \
	TextAPI \
	BinaryFormat \
	FrontendDirective

.include "${.PARSEDIR}/../../link.mk"

.include <bsd.prog.mk>
