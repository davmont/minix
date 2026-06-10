#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:53 joerg Exp $

LIB=	LLVMMC

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/lib/MC

SRCS+=	ConstantPools.cpp \
	DXContainerPSVInfo.cpp \
	DXContainerRootSignature.cpp \
	ELFObjectWriter.cpp \
	GOFFObjectWriter.cpp \
	MCAsmBackend.cpp \
	MCAsmInfo.cpp \
	MCAsmInfoCOFF.cpp \
	MCAsmInfoDarwin.cpp \
	MCAsmInfoELF.cpp \
	MCAsmInfoGOFF.cpp \
	MCAsmInfoWasm.cpp \
	MCAsmInfoXCOFF.cpp \
	MCAsmMacro.cpp \
	MCAsmStreamer.cpp \
	MCAssembler.cpp \
	MCCodeEmitter.cpp \
	MCCodeView.cpp \
	MCContext.cpp \
	MCDXContainerStreamer.cpp \
	MCDXContainerWriter.cpp \
	MCDwarf.cpp \
	MCELFObjectTargetWriter.cpp \
	MCELFStreamer.cpp \
	MCExpr.cpp \
	MCFragment.cpp \
	MCGOFFStreamer.cpp \
	MCInst.cpp \
	MCInstPrinter.cpp \
	MCInstrAnalysis.cpp \
	MCInstrDesc.cpp \
	MCInstrInfo.cpp \
	MCLabel.cpp \
	MCLinkerOptimizationHint.cpp \
	MCMachOStreamer.cpp \
	MCMachObjectTargetWriter.cpp \
	MCNullStreamer.cpp \
	MCObjectFileInfo.cpp \
	MCObjectStreamer.cpp \
	MCObjectWriter.cpp \
	MCPseudoProbe.cpp \
	MCRegisterInfo.cpp \
	MCSFrame.cpp \
	MCSPIRVStreamer.cpp \
	MCSchedule.cpp \
	MCSection.cpp \
	MCSectionMachO.cpp \
	MCStreamer.cpp \
	MCSubtargetInfo.cpp \
	MCSymbol.cpp \
	MCSymbolELF.cpp \
	MCSymbolGOFF.cpp \
	MCSymbolXCOFF.cpp \
	MCTargetOptions.cpp \
	MCTargetOptionsCommandFlags.cpp \
	MCValue.cpp \
	MCWasmObjectTargetWriter.cpp \
	MCWasmStreamer.cpp \
	MCWin64EH.cpp \
	MCWinCOFFStreamer.cpp \
	MCWinEH.cpp \
	MCXCOFFObjectTargetWriter.cpp \
	MCXCOFFStreamer.cpp \
	MachObjectWriter.cpp \
	SPIRVObjectWriter.cpp \
	StringTableBuilder.cpp \
	TargetRegistry.cpp \
	WasmObjectWriter.cpp \
	WinCOFFObjectWriter.cpp \
	XCOFFObjectWriter.cpp

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
