#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:56 joerg Exp $

LIB=	clangBasic

.include <bsd.init.mk>

.PATH: ${CLANG_SRCDIR}/lib/Basic
.PATH: ${CLANG_SRCDIR}/lib/Basic/Targets

SRCS+=	AArch64.cpp \
	AMDGPU.cpp \
	ARC.cpp \
	ARM.cpp \
	ASTSourceDescriptor.cpp \
	AVR.cpp \
	Attributes.cpp \
	BPF.cpp \
	Builtins.cpp \
	CLWarnings.cpp \
	CSKY.cpp \
	CharInfo.cpp \
	CodeGenOptions.cpp \
	Cuda.cpp \
	DarwinSDKInfo.cpp \
	Diagnostic.cpp \
	DiagnosticIDs.cpp \
	DiagnosticOptions.cpp \
	DirectX.cpp \
	ExpressionTraits.cpp \
	FileEntry.cpp \
	FileManager.cpp \
	FileSystemStatCache.cpp \
	Hexagon.cpp \
	IdentifierTable.cpp \
	Lanai.cpp \
	LangOptions.cpp \
	LangStandards.cpp \
	LoongArch.cpp \
	M68k.cpp \
	MSP430.cpp \
	MakeSupport.cpp \
	Mips.cpp \
	Module.cpp \
	NVPTX.cpp \
	NoSanitizeList.cpp \
	OSTargets.cpp \
	ObjCRuntime.cpp \
	OffloadArch.cpp \
	OpenCLOptions.cpp \
	OpenMPKinds.cpp \
	OperatorPrecedence.cpp \
	PPC.cpp \
	ParsedAttrInfo.cpp \
	ProfileList.cpp \
	RISCV.cpp \
	SPIR.cpp \
	SanitizerSpecialCaseList.cpp \
	Sanitizers.cpp \
	Sarif.cpp \
	SimpleTypoCorrection.cpp \
	SourceLocation.cpp \
	SourceManager.cpp \
	SourceMgrAdapter.cpp \
	Sparc.cpp \
	Stack.cpp \
	StackExhaustionHandler.cpp \
	SystemZ.cpp \
	TCE.cpp \
	TargetID.cpp \
	TargetInfo.cpp \
	Targets.cpp \
	TokenKinds.cpp \
	TypeTraits.cpp \
	VE.cpp \
	Version.cpp \
	Warnings.cpp \
	WebAssembly.cpp \
	X86.cpp \
	XCore.cpp \
	XRayInstr.cpp \
	XRayLists.cpp \
	Xtensa.cpp



CPPFLAGS+=	-I${CLANG_SRCDIR}/lib/Basic -I.

VCSVersion.inc: ${LLVM_TOPLEVEL}/Makefile.inc
	echo '#define LLVM_REVISION "${LLVM_REVISION}"' > VCSVersion.inc.tmp
	echo '#define LLVM_REPOSITORY "git@github.com:llvm/llvm-project.git"' >> VCSVersion.inc.tmp
	echo '#define CLANG_REVISION "${CLANG_REVISION}"' >> VCSVersion.inc.tmp
	echo '#define CLANG_REPOSITORY "git@github.com:llvm/llvm-project.git"' >> VCSVersion.inc.tmp
	mv VCSVersion.inc.tmp VCSVersion.inc

DPSRCS+=	VCSVersion.inc
CLEANFILES+=	VCSVersion.inc

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
