#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:48 joerg Exp $

PROG_CXX=	llvm-link
NOMAN=		yes

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/tools/llvm-link

SRCS=	llvm-link.cpp

LLVM_LIBS+= \
	IRReader \
	AsmParser \
	BitReader \
	BitstreamReader \
	BitWriter \
	Object \
	MCParser \
	MC \
	ipo \
	Linker \
	TransformsUtils \
	Analysis \
	Target \
	IR \
	Remarks \
	BinaryFormat \
	Support \
	Demangle

LLVM_LIBS+=	TargetParser

LLVM_LIBS+= \
	ipo \
	FrontendOpenMP \
	FrontendOffloading \
	ObjectYAML \
	FrontendAtomic \
	FrontendDirective \
	ScalarOpts \
	TransformsAggressiveInstCombine \
	InstCombine \
	Vectorize \
	SandboxIR \
	Instrumentation \
	FrontendHLSL \
	ProfileData \
	DebugInfoSymbolize \
	DebugInfoGSYM \
	DebugInfoPDB \
	DebugInfoCodeView \
	DebugInfoMSF \
	DebugInfoBTF \
	DebugInfoDWARF \
	TextAPI

.include "${.PARSEDIR}/../../link.mk"

.include <bsd.prog.mk>
