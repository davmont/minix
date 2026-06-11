#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:51 joerg Exp $

LIB=	LLVMDebugInfoDWARF

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/lib/DebugInfo/DWARF
.PATH: ${LLVM_SRCDIR}/lib/DebugInfo/DWARF/LowLevel

SRCS+=	DWARFAbbreviationDeclaration.cpp \
	DWARFAcceleratorTable.cpp \
	DWARFAddressRange.cpp \
	DWARFCFIPrinter.cpp \
	DWARFCFIProgram.cpp \
	DWARFCompileUnit.cpp \
	DWARFContext.cpp \
	DWARFDebugAbbrev.cpp \
	DWARFDebugAddr.cpp \
	DWARFDebugArangeSet.cpp \
	DWARFDebugAranges.cpp \
	DWARFDebugFrame.cpp \
	DWARFDebugInfoEntry.cpp \
	DWARFDebugLine.cpp \
	DWARFDebugLoc.cpp \
	DWARFDebugMacro.cpp \
	DWARFDebugPubTable.cpp \
	DWARFDebugRangeList.cpp \
	DWARFDebugRnglists.cpp \
	DWARFDie.cpp \
	DWARFExpression.cpp \
	DWARFExpressionPrinter.cpp \
	DWARFFormValue.cpp \
	DWARFGdbIndex.cpp \
	DWARFListTable.cpp \
	DWARFLocationExpression.cpp \
	DWARFTypeUnit.cpp \
	DWARFUnit.cpp \
	DWARFUnitIndex.cpp \
	DWARFUnwindTable.cpp \
	DWARFUnwindTablePrinter.cpp \
	DWARFVerifier.cpp

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
