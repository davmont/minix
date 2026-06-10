#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:49 joerg Exp $

PROG_CXX=	llvm-size
NOMAN=		yes

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/tools/llvm-size

SRCS=	llvm-size.cpp

LLVM_LIBS+= \
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

.include "${.PARSEDIR}/../../driver.mk"

LLVM_LIBS+=	TargetParser

LLVM_LIBS+= \
	Option \
	TargetParser \
	IRReader \
	AsmParser \
	DebugInfoDWARF

.include "${.PARSEDIR}/../../link.mk"

.include <bsd.prog.mk>
