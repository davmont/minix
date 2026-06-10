#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:56 joerg Exp $

LIB=	clangDriver

.include <bsd.init.mk>

.PATH: ${CLANG_SRCDIR}/lib/Driver

CPPFLAGS+=	-I${CLANG_SRCDIR}/lib/Driver

SRCS+=	AArch64.cpp \
	AIX.cpp \
	AMDGPU.cpp \
	AMDGPUOpenMP.cpp \
	ARM.cpp \
	AVR.cpp \
	Action.cpp \
	BareMetal.cpp \
	CSKY.cpp \
	CSKYToolChain.cpp \
	Clang.cpp \
	CommonArgs.cpp \
	Compilation.cpp \
	CreateASTUnitFromArgs.cpp \
	CreateInvocationFromArgs.cpp \
	CrossWindows.cpp \
	Cuda.cpp \
	Cygwin.cpp \
	Darwin.cpp \
	Distro.cpp \
	DragonFly.cpp \
	Driver.cpp \
	Flang.cpp \
	FreeBSD.cpp \
	Fuchsia.cpp \
	Gnu.cpp \
	HIPAMD.cpp \
	HIPSPV.cpp \
	HIPUtility.cpp \
	HLSL.cpp \
	Haiku.cpp \
	Hexagon.cpp \
	Hurd.cpp \
	InterfaceStubs.cpp \
	Job.cpp \
	LFILinux.cpp \
	Linux.cpp \
	LoongArch.cpp \
	M68k.cpp \
	MSP430.cpp \
	MSVC.cpp \
	Managarm.cpp \
	MinGW.cpp \
	Minix.cpp \
	Mips.cpp \
	MipsLinux.cpp \
	Multilib.cpp \
	MultilibBuilder.cpp \
	NetBSD.cpp \
	OHOS.cpp \
	OffloadBundler.cpp \
	OpenBSD.cpp \
	PPC.cpp \
	PPCFreeBSD.cpp \
	PPCLinux.cpp \
	PS4CPU.cpp \
	Phases.cpp \
	RISCV.cpp \
	SPIRV.cpp \
	SPIRVOpenMP.cpp \
	SYCL.cpp \
	SanitizerArgs.cpp \
	Solaris.cpp \
	Sparc.cpp \
	SystemZ.cpp \
	TCE.cpp \
	Tool.cpp \
	ToolChain.cpp \
	Types.cpp \
	UEFI.cpp \
	VE.cpp \
	VEToolchain.cpp \
	WebAssembly.cpp \
	X86.cpp \
	XCore.cpp \
	XRayArgs.cpp \
	ZOS.cpp

.PATH: ${CLANG_SRCDIR}/lib/Driver/ToolChains


.PATH: ${CLANG_SRCDIR}/lib/Driver/ToolChains/Arch





.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
