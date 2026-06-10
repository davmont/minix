#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:50 joerg Exp $

LIB=	LLVMARMCodeGen

.include <bsd.init.mk>

CPPFLAGS+=	-I${LLVM_SRCDIR}/lib/Target/ARM

.PATH: ${LLVM_SRCDIR}/lib/Target/ARM

SRCS+=	A15SDOptimizer.cpp \
	ARMAsmPrinter.cpp \
	ARMBaseInstrInfo.cpp \
	ARMBaseRegisterInfo.cpp \
	ARMBasicBlockInfo.cpp \
	ARMBlockPlacement.cpp \
	ARMBranchTargets.cpp \
	ARMCallLowering.cpp \
	ARMCallingConv.cpp \
	ARMConstantIslandPass.cpp \
	ARMConstantPoolValue.cpp \
	ARMExpandPseudoInsts.cpp \
	ARMFastISel.cpp \
	ARMFixCortexA57AES1742098Pass.cpp \
	ARMFrameLowering.cpp \
	ARMHazardRecognizer.cpp \
	ARMISelDAGToDAG.cpp \
	ARMISelLowering.cpp \
	ARMInstrInfo.cpp \
	ARMInstructionSelector.cpp \
	ARMLatencyMutations.cpp \
	ARMLegalizerInfo.cpp \
	ARMLoadStoreOptimizer.cpp \
	ARMLowOverheadLoops.cpp \
	ARMMCInstLower.cpp \
	ARMMachineFunctionInfo.cpp \
	ARMMacroFusion.cpp \
	ARMOptimizeBarriersPass.cpp \
	ARMParallelDSP.cpp \
	ARMRegisterBankInfo.cpp \
	ARMRegisterInfo.cpp \
	ARMSLSHardening.cpp \
	ARMSelectionDAGInfo.cpp \
	ARMSubtarget.cpp \
	ARMTargetMachine.cpp \
	ARMTargetObjectFile.cpp \
	ARMTargetTransformInfo.cpp \
	MLxExpansionPass.cpp \
	MVEGatherScatterLowering.cpp \
	MVELaneInterleavingPass.cpp \
	MVETPAndVPTOptimisationsPass.cpp \
	MVETailPredication.cpp \
	MVEVPTBlockPass.cpp \
	Thumb1FrameLowering.cpp \
	Thumb1InstrInfo.cpp \
	Thumb2ITBlockPass.cpp \
	Thumb2InstrInfo.cpp \
	Thumb2SizeReduction.cpp \
	ThumbRegisterInfo.cpp

TABLEGEN_SRC=		ARM.td
TABLEGEN_INCLUDES=	-I${LLVM_SRCDIR}/lib/Target/ARM
TABLEGEN_OUTPUT= \
	ARMGenSDNodeInfo.inc|-gen-sd-node-info \
	ARMGenAsmMatcher.inc|-gen-asm-matcher \
	ARMGenAsmWriter.inc|-gen-asm-writer \
	ARMGenCallingConv.inc|-gen-callingconv \
	ARMGenCodeEmitter.inc|-gen-emitter \
	ARMGenDAGISel.inc|-gen-dag-isel \
	ARMGenDisassemblerTables.inc|-gen-disassembler \
	ARMGenFastISel.inc|-gen-fast-isel \
	ARMGenGlobalISel.inc|-gen-global-isel \
	ARMGenInstrInfo.inc|-gen-instr-info \
	ARMGenMCCodeEmitter.inc|-gen-emitter \
	ARMGenMCPseudoLowering.inc|-gen-pseudo-lowering \
	ARMGenRegisterBank.inc|-gen-register-bank \
	ARMGenRegisterInfo.inc|-gen-register-info \
	ARMGenSubtargetInfo.inc|-gen-subtarget \
	ARMGenSystemRegister.inc|-gen-searchable-tables


.include "${.PARSEDIR}/../../tablegen.mk"

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
