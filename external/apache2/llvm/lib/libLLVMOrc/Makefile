#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:53 joerg Exp $

LIB=	LLVMOrc

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/lib/ExecutionEngine/Orc

SRCS+=	AbsoluteSymbols.cpp \
	BacktraceTools.cpp \
	COFF.cpp \
	COFFPlatform.cpp \
	COFFVCRuntimeSupport.cpp \
	CompileOnDemandLayer.cpp \
	CompileUtils.cpp \
	Core.cpp \
	DebugUtils.cpp \
	Debugging/DebugInfoSupport.cpp \
	Debugging/DebuggerSupport.cpp \
	Debugging/DebuggerSupportPlugin.cpp \
	Debugging/ELFDebugObjectPlugin.cpp \
	Debugging/LLJITUtilsCBindings.cpp \
	Debugging/PerfSupportPlugin.cpp \
	Debugging/VTuneSupportPlugin.cpp \
	EHFrameRegistrationPlugin.cpp \
	ELFNixPlatform.cpp \
	EPCDynamicLibrarySearchGenerator.cpp \
	EPCGenericDylibManager.cpp \
	EPCGenericJITLinkMemoryManager.cpp \
	EPCGenericRTDyldMemoryManager.cpp \
	EPCIndirectionUtils.cpp \
	ExecutionUtils.cpp \
	ExecutorProcessControl.cpp \
	ExecutorResolutionGenerator.cpp \
	IRCompileLayer.cpp \
	IRPartitionLayer.cpp \
	IRTransformLayer.cpp \
	InProcessMemoryAccess.cpp \
	IndirectionUtils.cpp \
	JITLinkRedirectableSymbolManager.cpp \
	JITLinkReentryTrampolines.cpp \
	JITTargetMachineBuilder.cpp \
	LLJIT.cpp \
	Layer.cpp \
	LazyObjectLinkingLayer.cpp \
	LazyReexports.cpp \
	LinkGraphLayer.cpp \
	LinkGraphLinkingLayer.cpp \
	LoadLinkableFile.cpp \
	LookupAndRecordAddrs.cpp \
	MachO.cpp \
	MachOPlatform.cpp \
	Mangling.cpp \
	MapperJITLinkMemoryManager.cpp \
	MemoryMapper.cpp \
	ObjectFileInterface.cpp \
	ObjectLinkingLayer.cpp \
	ObjectTransformLayer.cpp \
	OrcABISupport.cpp \
	OrcV2CBindings.cpp \
	RTDyldObjectLinkingLayer.cpp \
	ReOptimizeLayer.cpp \
	RedirectionManager.cpp \
	SectCreate.cpp \
	SelfExecutorProcessControl.cpp \
	SimpleRemoteEPC.cpp \
	SimpleRemoteMemoryMapper.cpp \
	SpeculateAnalyses.cpp \
	Speculation.cpp \
	TaskDispatch.cpp \
	ThreadSafeModule.cpp \
	UnwindInfoRegistrationPlugin.cpp

.include <bsd.lib.mk>
