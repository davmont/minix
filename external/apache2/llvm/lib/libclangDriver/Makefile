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

SRCS+=	AMDGPU.cpp \
	Ananas.cpp \
	AIX.cpp \
	AMDGPUOpenMP.cpp \
	AVR.cpp \
	BareMetal.cpp \
	Clang.cpp \
	CloudABI.cpp \
	CommonArgs.cpp \
	Contiki.cpp \
	CrossWindows.cpp \
	Cuda.cpp \
	Darwin.cpp \
	DragonFly.cpp \
	Flang.cpp \
	FreeBSD.cpp \
	Fuchsia.cpp \
	Gnu.cpp \
	Haiku.cpp \
	Hexagon.cpp \
	HIP.cpp \
	Hurd.cpp \
	InterfaceStubs.cpp \
	Linux.cpp \
	M68k.cpp \
	MinGW.cpp \
	Minix.cpp \
	MipsLinux.cpp \
	MSP430.cpp \
	MSVC.cpp \
	Myriad.cpp \
	NaCl.cpp \
	NetBSD.cpp \
	OpenBSD.cpp \
	PPCLinux.cpp \
	PS4CPU.cpp \
	RISCVToolchain.cpp \
	Solaris.cpp \
	TCE.cpp \
	VEToolchain.cpp \
	WebAssembly.cpp \
	XCore.cpp \
	ZOS.cpp

.PATH: ${CLANG_SRCDIR}/lib/Driver/ToolChains/Arch

SRCS+=	AArch64.cpp \
	ARM.cpp \
	Mips.cpp \
	PPC.cpp \
	RISCV.cpp \
	Sparc.cpp \
	SystemZ.cpp \
	VE.cpp \
	X86.cpp




.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
