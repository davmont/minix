#	$NetBSD: Makefile,v 1.1 2021/05/30 01:56:52 joerg Exp $

LIB=	LLVMExecutionEngineOrcTargetProcess

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/lib/ExecutionEngine/Orc/TargetProcess

SRCS+=	DefaultHostBootstrapValues.cpp \
	ExecutorResolver.cpp \
	ExecutorSharedMemoryMapperService.cpp \
	JITLoaderGDB.cpp \
	JITLoaderPerf.cpp \
	JITLoaderVTune.cpp \
	LibraryResolver.cpp \
	LibraryScanner.cpp \
	OrcRTBootstrap.cpp \
	RegisterEHFrames.cpp \
	SimpleExecutorDylibManager.cpp \
	SimpleExecutorMemoryManager.cpp \
	SimpleRemoteEPCServer.cpp \
	TargetExecutionUtils.cpp \
	UnwindInfoManager.cpp

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
