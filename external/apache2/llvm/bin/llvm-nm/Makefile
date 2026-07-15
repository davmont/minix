#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:48 joerg Exp $

PROG_CXX=	llvm-nm
NOMAN=		yes

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/tools/llvm-nm

# llvm-nm.cpp includes Opts.inc, which tablegen generates from Opts.td.  The
# reachover never declared it -- it was not noticed because the tool is only
# built when MKLLVMCMDS is on, which it usually is not.
TABLEGEN_SRC=		Opts.td
TABLEGEN_OUTPUT= \
	Opts.inc|-gen-opt-parser-defs

.include "${.PARSEDIR}/../../tablegen.mk"

SRCS=	llvm-nm.cpp

# AMDGPU target libs are intentionally omitted: they are not built for the
# host (see the _BSD_HOSTINIT_MK_ guard in ../../lib/Makefile), and this tool
# does not reference any target back end.
LLVM_LIBS+= \
	DebugInfoSymbolize \
	DebugInfoGSYM \
	DebugInfoPDB \
	AArch64AsmParser \
	AArch64MCTargetDesc \
	AArch64TargetInfo \
	AArch64Utils \
	ARMAsmParser \
	ARMMCTargetDesc \
	ARMTargetInfo \
	ARMUtils \
	MipsAsmParser \
	MipsMCTargetDesc \
	MipsTargetInfo \
	PowerPCAsmParser \
	PowerPCMCTargetDesc \
	PowerPCTargetInfo \
	SparcTargetInfo \
	SparcMCTargetDesc \
	SparcAsmParser \
	X86AsmParser \
	X86CodeGen \
	X86MCTargetDesc \
	X86TargetInfo \
	CodeGen \
	MCDisassembler \
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

LDADD+=	-lz
# On a host build ${LIBZ} names the *host's* /usr/lib/libz.a, which is not ours
# to depend on ("don't know how to make /usr/lib/libz.a").  clang guards it the
# same way.
.if !defined(HOSTPROG)
DPADD+=	${LIBZ}
.endif

.include "${.PARSEDIR}/../../driver.mk"

LLVM_LIBS+=	TargetParser

LLVM_LIBS+= \
	Option \
	TargetParser \
	CodeGenTypes \
	Analysis \
	FrontendHLSL \
	ProfileData \
	DebugInfoCodeView \
	DebugInfoMSF \
	DebugInfoBTF \
	DebugInfoDWARF \
	IRReader \
	AsmParser

.include "${.PARSEDIR}/../../link.mk"

.include <bsd.prog.mk>
