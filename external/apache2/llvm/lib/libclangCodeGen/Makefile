#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:56 joerg Exp $

LIB=	clangCodeGen

.include <bsd.init.mk>

.PATH: ${CLANG_SRCDIR}/lib/CodeGen
.PATH: ${CLANG_SRCDIR}/lib/CodeGen/TargetBuiltins
.PATH: ${CLANG_SRCDIR}/lib/CodeGen/Targets

SRCS+=	AArch64.cpp \
	ABIInfo.cpp \
	ABIInfoImpl.cpp \
	AMDGPU.cpp \
	ARC.cpp \
	ARM.cpp \
	AVR.cpp \
	BPF.cpp \
	BackendUtil.cpp \
	CGAtomic.cpp \
	CGBlocks.cpp \
	CGBuiltin.cpp \
	CGCUDANV.cpp \
	CGCUDARuntime.cpp \
	CGCXX.cpp \
	CGCXXABI.cpp \
	CGCall.cpp \
	CGClass.cpp \
	CGCleanup.cpp \
	CGCoroutine.cpp \
	CGDebugInfo.cpp \
	CGDecl.cpp \
	CGDeclCXX.cpp \
	CGException.cpp \
	CGExpr.cpp \
	CGExprAgg.cpp \
	CGExprCXX.cpp \
	CGExprComplex.cpp \
	CGExprConstant.cpp \
	CGExprScalar.cpp \
	CGGPUBuiltin.cpp \
	CGHLSLBuiltins.cpp \
	CGHLSLRuntime.cpp \
	CGLoopInfo.cpp \
	CGNonTrivialStruct.cpp \
	CGObjC.cpp \
	CGObjCGNU.cpp \
	CGObjCMac.cpp \
	CGObjCRuntime.cpp \
	CGOpenCLRuntime.cpp \
	CGOpenMPRuntime.cpp \
	CGOpenMPRuntimeGPU.cpp \
	CGPointerAuth.cpp \
	CGRecordLayoutBuilder.cpp \
	CGStmt.cpp \
	CGStmtOpenMP.cpp \
	CGVTT.cpp \
	CGVTables.cpp \
	CSKY.cpp \
	CodeGenABITypes.cpp \
	CodeGenAction.cpp \
	CodeGenFunction.cpp \
	CodeGenModule.cpp \
	CodeGenPGO.cpp \
	CodeGenSYCL.cpp \
	CodeGenTBAA.cpp \
	CodeGenTypes.cpp \
	ConstantInitBuilder.cpp \
	CoverageMappingGen.cpp \
	DirectX.cpp \
	HLSLBufferLayoutBuilder.cpp \
	Hexagon.cpp \
	ItaniumCXXABI.cpp \
	Lanai.cpp \
	LinkInModulesPass.cpp \
	LoongArch.cpp \
	M68k.cpp \
	MSP430.cpp \
	MacroPPCallbacks.cpp \
	MicrosoftCXXABI.cpp \
	Mips.cpp \
	ModuleBuilder.cpp \
	NVPTX.cpp \
	ObjectFilePCHContainerWriter.cpp \
	PPC.cpp \
	PatternInit.cpp \
	RISCV.cpp \
	SPIR.cpp \
	SanitizerMetadata.cpp \
	Sparc.cpp \
	SwiftCallingConv.cpp \
	SystemZ.cpp \
	TCE.cpp \
	TargetInfo.cpp \
	TrapReasonBuilder.cpp \
	VE.cpp \
	VarBypassDetector.cpp \
	WebAssembly.cpp \
	X86.cpp \
	XCore.cpp

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
