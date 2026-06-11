#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:49 joerg Exp $

PROG_CXX=	llvm-tblgen
NOMAN=		yes

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/utils/TableGen
.PATH: ${LLVM_SRCDIR}/utils/TableGen/Basic
.PATH: ${LLVM_SRCDIR}/utils/TableGen/Common
.PATH: ${LLVM_SRCDIR}/utils/TableGen/Common/GlobalISel

CPPFLAGS+=	-I${LLVM_SRCDIR}/utils/TableGen

SRCS=	ARMTargetDefEmitter.cpp \
	AsmMatcherEmitter.cpp \
	AsmWriterEmitter.cpp \
	AsmWriterInst.cpp \
	Attributes.cpp \
	CTagsEmitter.cpp \
	CXXPredicates.cpp \
	CallingConvEmitter.cpp \
	CodeEmitterGen.cpp \
	CodeExpander.cpp \
	CodeGenDAGPatterns.cpp \
	CodeGenHwModes.cpp \
	CodeGenInstAlias.cpp \
	CodeGenInstruction.cpp \
	CodeGenIntrinsics.cpp \
	CodeGenMapTable.cpp \
	CodeGenRegisters.cpp \
	CodeGenSchedule.cpp \
	CodeGenTarget.cpp \
	CombinerUtils.cpp \
	CompressInstEmitter.cpp \
	DAGISelEmitter.cpp \
	DAGISelMatcher.cpp \
	DAGISelMatcherEmitter.cpp \
	DAGISelMatcherGen.cpp \
	DAGISelMatcherOpt.cpp \
	DFAEmitter.cpp \
	DFAPacketizerEmitter.cpp \
	DXILEmitter.cpp \
	DecoderEmitter.cpp \
	DecoderTableEmitter.cpp \
	DecoderTree.cpp \
	DirectiveEmitter.cpp \
	DisassemblerEmitter.cpp \
	ExegesisEmitter.cpp \
	FastISelEmitter.cpp \
	GlobalISelCombinerEmitter.cpp \
	GlobalISelEmitter.cpp \
	GlobalISelMatchTable.cpp \
	GlobalISelMatchTableExecutorEmitter.cpp \
	InfoByHwMode.cpp \
	InstrDocsEmitter.cpp \
	InstrInfoEmitter.cpp \
	InstructionEncoding.cpp \
	IntrinsicEmitter.cpp \
	MacroFusionPredicatorEmitter.cpp \
	OptEmitter.cpp \
	OptionParserEmitter.cpp \
	OptionRSTEmitter.cpp \
	PatternParser.cpp \
	Patterns.cpp \
	PredicateExpander.cpp \
	PseudoLoweringEmitter.cpp \
	RISCVTargetDefEmitter.cpp \
	RegisterBankEmitter.cpp \
	RegisterInfoEmitter.cpp \
	RuntimeLibcalls.cpp \
	RuntimeLibcallsEmitter.cpp \
	SDNodeInfoEmitter.cpp \
	SDNodeProperties.cpp \
	SearchableTableEmitter.cpp \
	SubtargetEmitter.cpp \
	SubtargetFeatureInfo.cpp \
	TableGen.cpp \
	TargetFeaturesEmitter.cpp \
	TargetLibraryInfoEmitter.cpp \
	Types.cpp \
	Utils.cpp \
	VTEmitter.cpp \
	VarLenCodeEmitterGen.cpp \
	WebAssemblyDisassemblerEmitter.cpp \
	X86DisassemblerTables.cpp \
	X86FoldTablesEmitter.cpp \
	X86InstrMappingEmitter.cpp \
	X86MnemonicTables.cpp \
	X86ModRMFilters.cpp \
	X86RecognizableInstr.cpp \
	llvm-tblgen.cpp

.PATH: ${LLVM_SRCDIR}/utils/TableGen
.PATH: ${LLVM_SRCDIR}/utils/TableGen/Basic
.PATH: ${LLVM_SRCDIR}/utils/TableGen/Common
.PATH: ${LLVM_SRCDIR}/utils/TableGen/Common/GlobalISel

LLVM_LIBS+= \
	TableGen \
	Support \
	Demangle

.include "${.PARSEDIR}/../../link.mk"

.include <bsd.prog.mk>
