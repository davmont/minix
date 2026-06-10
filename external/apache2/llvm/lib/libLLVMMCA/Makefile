#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:53 joerg Exp $

LIB=	LLVMMCA

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/lib/MCA

SRCS+=	CodeEmitter.cpp \
	Context.cpp \
	CustomBehaviour.cpp \
	DispatchStage.cpp \
	EntryStage.cpp \
	ExecuteStage.cpp \
	HWEventListener.cpp \
	HardwareUnit.cpp \
	InOrderIssueStage.cpp \
	IncrementalSourceMgr.cpp \
	InstrBuilder.cpp \
	Instruction.cpp \
	InstructionTables.cpp \
	LSUnit.cpp \
	MicroOpQueueStage.cpp \
	Pipeline.cpp \
	RegisterFile.cpp \
	ResourceManager.cpp \
	RetireControlUnit.cpp \
	RetireStage.cpp \
	Scheduler.cpp \
	Stage.cpp \
	Support.cpp \
	View.cpp

.PATH: ${LLVM_SRCDIR}/lib/MCA/HardwareUnits

SRCS+=	HardwareUnit.cpp \
	LSUnit.cpp \
	RegisterFile.cpp \
	ResourceManager.cpp \
	RetireControlUnit.cpp \
	Scheduler.cpp

.PATH: ${LLVM_SRCDIR}/lib/MCA/Stages

SRCS+=	DispatchStage.cpp \
	EntryStage.cpp \
	ExecuteStage.cpp \
	InstructionTables.cpp \
	MicroOpQueueStage.cpp \
	RetireStage.cpp \
	Stage.cpp

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
