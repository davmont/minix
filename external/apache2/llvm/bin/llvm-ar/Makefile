#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:47 joerg Exp $

PROG_CXX=	llvm-ar
NOMAN=		yes

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/tools/llvm-ar

SRCS=	llvm-ar.cpp

# AMDGPU target libs are intentionally omitted: they are not built for the
# host (see the _BSD_HOSTINIT_MK_ guard in ../../lib/Makefile), and this tool
# does not reference any target back end.
LLVM_LIBS+= \
	ToolDriversDlltool \
	ToolDrivers \
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
	MCParser \
	MC \
	BitReader \
	BitstreamReader \
	IR \
	Remarks \
	BinaryFormat \
	Option \
	Support \
	Demangle

LDADD+=	-lz
# On a host build ${LIBZ} names the *host's* /usr/lib/libz.a, which is not ours
# to depend on ("don't know how to make /usr/lib/libz.a").  clang guards it the
# same way.
.if !defined(HOSTPROG)
DPADD+=	${LIBZ}
.endif

LLVM_DRIVER_TOOL=	llvm_ar
.include "${.PARSEDIR}/../../driver.mk"

LLVM_LIBS+=	TargetParser

LLVM_LIBS+= \
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
	AsmParser \
	TextAPI

.include "${.PARSEDIR}/../../link.mk"

.include <bsd.prog.mk>
