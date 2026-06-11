#	$NetBSD: Makefile,v 1.1 2019/11/11 22:45:09 joerg Exp $

LIB=	LLVMMipsCodeGen

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/lib/Target/Mips
# LLVM 22 MCTargetDesc headers cross-include via the MCTargetDesc/ prefix.
CPPFLAGS+=	-I${LLVM_SRCDIR}/lib/Target/Mips

SRCS+=	MicroMipsSizeReduction.cpp \
	Mips16FrameLowering.cpp \
	Mips16HardFloat.cpp \
	Mips16HardFloatInfo.cpp \
	Mips16ISelDAGToDAG.cpp \
	Mips16ISelLowering.cpp \
	Mips16InstrInfo.cpp \
	Mips16RegisterInfo.cpp \
	MipsAnalyzeImmediate.cpp \
	MipsAsmPrinter.cpp \
	MipsBranchExpansion.cpp \
	MipsCCState.cpp \
	MipsCallLowering.cpp \
	MipsConstantIslandPass.cpp \
	MipsDelaySlotFiller.cpp \
	MipsExpandPseudo.cpp \
	MipsFastISel.cpp \
	MipsFrameLowering.cpp \
	MipsISelDAGToDAG.cpp \
	MipsISelLowering.cpp \
	MipsInstrInfo.cpp \
	MipsInstructionSelector.cpp \
	MipsLegalizerInfo.cpp \
	MipsMCInstLower.cpp \
	MipsMachineFunction.cpp \
	MipsModuleISelDAGToDAG.cpp \
	MipsMulMulBugPass.cpp \
	MipsOptimizePICCall.cpp \
	MipsOs16.cpp \
	MipsPostLegalizerCombiner.cpp \
	MipsPreLegalizerCombiner.cpp \
	MipsRegisterBankInfo.cpp \
	MipsRegisterInfo.cpp \
	MipsSEFrameLowering.cpp \
	MipsSEISelDAGToDAG.cpp \
	MipsSEISelLowering.cpp \
	MipsSEInstrInfo.cpp \
	MipsSERegisterInfo.cpp \
	MipsSelectionDAGInfo.cpp \
	MipsSubtarget.cpp \
	MipsTargetMachine.cpp \
	MipsTargetObjectFile.cpp \
	MipsTargetTransformInfo.cpp

TABLEGEN_SRC=		Mips.td
# LLVM 22: Mips GlobalISel now emits a post-legalizer combiner header.
TABLEGEN_INCLUDES=	-I${LLVM_SRCDIR}/lib/Target/Mips \
			-I${LLVM_SRCDIR}/lib/Target
TABLEGEN_OUTPUT= \
	MipsGenSDNodeInfo.inc|-gen-sd-node-info \
	MipsGenExegesis.inc|-gen-exegesis \
	MipsGenPostLegalizeGICombiner.inc|-gen-global-isel-combiner^-combiners=MipsPostLegalizerCombiner \
	MipsGenAsmMatcher.inc|-gen-asm-matcher \
	MipsGenAsmWriter.inc|-gen-asm-writer \
	MipsGenCallingConv.inc|-gen-callingconv \
	MipsGenCodeEmitter.inc|-gen-emitter \
	MipsGenDAGISel.inc|-gen-dag-isel \
	MipsGenDisassemblerTables.inc|-gen-disassembler^-ignore-non-decodable-operands \
	MipsGenFastISel.inc|-gen-fast-isel \
	MipsGenGlobalISel.inc|-gen-global-isel \
	MipsGenInstrInfo.inc|-gen-instr-info \
	MipsGenMCCodeEmitter.inc|-gen-emitter \
	MipsGenMCPseudoLowering.inc|-gen-pseudo-lowering \
	MipsGenRegisterBank.inc|-gen-register-bank \
	MipsGenRegisterInfo.inc|-gen-register-info \
	MipsGenSubtargetInfo.inc|-gen-subtarget

.include "${.PARSEDIR}/../../tablegen.mk"

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
