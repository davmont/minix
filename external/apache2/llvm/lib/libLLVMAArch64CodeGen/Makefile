#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:50 joerg Exp $

LIB=	LLVMAArch64CodeGen

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/lib/Target/AArch64
.PATH: ${LLVM_SRCDIR}/lib/Target/AArch64/GISel

CPPFLAGS+=	-I${LLVM_SRCDIR}/lib/Target/AArch64

SRCS+=	AArch64A53Fix835769.cpp \
	AArch64A57FPLoadBalancing.cpp \
	AArch64AdvSIMDScalarPass.cpp \
	AArch64Arm64ECCallLowering.cpp \
	AArch64AsmPrinter.cpp \
	AArch64BranchTargets.cpp \
	AArch64CallLowering.cpp \
	AArch64CallingConvention.cpp \
	AArch64CleanupLocalDynamicTLSPass.cpp \
	AArch64CollectLOH.cpp \
	AArch64CompressJumpTables.cpp \
	AArch64CondBrTuning.cpp \
	AArch64ConditionOptimizer.cpp \
	AArch64ConditionalCompares.cpp \
	AArch64DeadRegisterDefinitionsPass.cpp \
	AArch64ExpandImm.cpp \
	AArch64ExpandPseudoInsts.cpp \
	AArch64FalkorHWPFFix.cpp \
	AArch64FastISel.cpp \
	AArch64FrameLowering.cpp \
	AArch64GlobalISelUtils.cpp \
	AArch64ISelDAGToDAG.cpp \
	AArch64ISelLowering.cpp \
	AArch64InstrInfo.cpp \
	AArch64InstructionSelector.cpp \
	AArch64LegalizerInfo.cpp \
	AArch64LoadStoreOptimizer.cpp \
	AArch64LowerHomogeneousPrologEpilog.cpp \
	AArch64MCInstLower.cpp \
	AArch64MIPeepholeOpt.cpp \
	AArch64MachineFunctionInfo.cpp \
	AArch64MachineScheduler.cpp \
	AArch64MacroFusion.cpp \
	AArch64O0PreLegalizerCombiner.cpp \
	AArch64PBQPRegAlloc.cpp \
	AArch64PointerAuth.cpp \
	AArch64PostCoalescerPass.cpp \
	AArch64PostLegalizerCombiner.cpp \
	AArch64PostLegalizerLowering.cpp \
	AArch64PostSelectOptimize.cpp \
	AArch64PreLegalizerCombiner.cpp \
	AArch64PrologueEpilogue.cpp \
	AArch64PromoteConstant.cpp \
	AArch64RedundantCondBranchPass.cpp \
	AArch64RedundantCopyElimination.cpp \
	AArch64RegisterBankInfo.cpp \
	AArch64RegisterInfo.cpp \
	AArch64SIMDInstrOpt.cpp \
	AArch64SLSHardening.cpp \
	AArch64SMEAttributes.cpp \
	AArch64SelectionDAGInfo.cpp \
	AArch64SpeculationHardening.cpp \
	AArch64StackTagging.cpp \
	AArch64StackTaggingPreRA.cpp \
	AArch64StorePairSuppress.cpp \
	AArch64Subtarget.cpp \
	AArch64TargetMachine.cpp \
	AArch64TargetObjectFile.cpp \
	AArch64TargetTransformInfo.cpp \
	MachineSMEABIPass.cpp \
	SMEABIPass.cpp \
	SMEPeepholeOpt.cpp \
	SVEIntrinsicOpts.cpp



TABLEGEN_SRC=		AArch64.td
# LLVM 22: GlobalISel combiners are referenced by name without the "Helper"
# suffix and need lib/Target on the include path; the standalone
# AArch64GenGICombiner.inc output was dropped upstream.
TABLEGEN_INCLUDES=	-I${LLVM_SRCDIR}/lib/Target/AArch64 \
			-I${LLVM_SRCDIR}/lib/Target
TABLEGEN_OUTPUT= \
	AArch64GenSDNodeInfo.inc|-gen-sd-node-info \
	AArch64GenAsmMatcher.inc|-gen-asm-matcher \
	AArch64GenAsmWriter1.inc|-gen-asm-writer^-asmwriternum=1 \
	AArch64GenAsmWriter.inc|-gen-asm-writer \
	AArch64GenCallingConv.inc|-gen-callingconv \
	AArch64GenDAGISel.inc|-gen-dag-isel \
	AArch64GenDisassemblerTables.inc|-gen-disassembler \
	AArch64GenExegesis.inc|-gen-exegesis \
	AArch64GenFastISel.inc|-gen-fast-isel \
	AArch64GenGlobalISel.inc|-gen-global-isel \
	AArch64GenInstrInfo.inc|-gen-instr-info \
	AArch64GenMCCodeEmitter.inc|-gen-emitter \
	AArch64GenMCPseudoLowering.inc|-gen-pseudo-lowering \
	AArch64GenO0PreLegalizeGICombiner.inc|-gen-global-isel-combiner^-combiners=AArch64O0PreLegalizerCombiner \
	AArch64GenPreLegalizeGICombiner.inc|-gen-global-isel-combiner^-combiners=AArch64PreLegalizerCombiner \
	AArch64GenPostLegalizeGICombiner.inc|-gen-global-isel-combiner^-combiners=AArch64PostLegalizerCombiner \
	AArch64GenPostLegalizeGILowering.inc|-gen-global-isel-combiner^-combiners=AArch64PostLegalizerLowering \
	AArch64GenRegisterBank.inc|-gen-register-bank \
	AArch64GenRegisterInfo.inc|-gen-register-info \
	AArch64GenSubtargetInfo.inc|-gen-subtarget \
	AArch64GenSystemOperands.inc|-gen-searchable-tables


.include "${.PARSEDIR}/../../tablegen.mk"

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
