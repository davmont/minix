#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:49 joerg Exp $

PROG_CXX=	sanstats
NOMAN=		yes

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/tools/sanstats

SRCS=	sanstats.cpp

LLVM_LIBS+= \
	DebugInfoSymbolize \
	DebugInfoDWARF \
	DebugInfoPDB \
	DebugInfoMSF \
	DebugInfoCodeView \
	Object \
	TextAPI \
	MCParser \
	MC \
	BitReader \
	BitstreamReader \
	IR \
	Remarks \
	BinaryFormat \
	Support \
	Demangle

LLVM_LIBS+=	TargetParser

LLVM_LIBS+= \
	DebugInfoGSYM \
	DebugInfoBTF \
	IRReader \
	AsmParser

.include "${.PARSEDIR}/../../link.mk"

LDADD+=	-lz
.if !defined(HOSTPROG)
DPADD+=	${LIBZ}
.endif

.include <bsd.prog.mk>
