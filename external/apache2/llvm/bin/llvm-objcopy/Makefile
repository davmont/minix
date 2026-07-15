#	MINIX: llvm-objcopy.  Rewritten for LLVM 22, which moved the object-copy
#	engine out of the tool and into libLLVMObjCopy; the tool is now just the
#	driver and its option parsing.  (The old reachover inlined the whole
#	COFF/ELF/MachO/wasm engine here, which no longer exists in this tree.)

PROG_CXX=	llvm-objcopy
NOMAN=		yes

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/tools/llvm-objcopy

SRCS=	llvm-objcopy.cpp \
	ObjcopyOptions.cpp

# ObjcopyOpts.td and StripOpts.td both "include CommonOpts.td", so tablegen has
# to be told where to look, or it fails with "could not find include file".
TABLEGEN_INCLUDES+=	-I${LLVM_SRCDIR}/tools/llvm-objcopy
TABLEGEN_SRC=		CommonOpts.td ObjcopyOpts.td StripOpts.td \
			InstallNameToolOpts.td BitcodeStripOpts.td

TABLEGEN_OUTPUT.CommonOpts.td= \
	CommonOpts.inc|-gen-opt-parser-defs
TABLEGEN_OUTPUT.ObjcopyOpts.td= \
	ObjcopyOpts.inc|-gen-opt-parser-defs
TABLEGEN_OUTPUT.StripOpts.td= \
	StripOpts.inc|-gen-opt-parser-defs
TABLEGEN_OUTPUT.InstallNameToolOpts.td= \
	InstallNameToolOpts.inc|-gen-opt-parser-defs
TABLEGEN_OUTPUT.BitcodeStripOpts.td= \
	BitcodeStripOpts.inc|-gen-opt-parser-defs

.include "${.PARSEDIR}/../../tablegen.mk"

LLVM_LIBS+= \
	ObjCopy \
	IRReader \
	AsmParser \
	DebugInfoDWARF \
	DebugInfoCodeView \
	DebugInfoMSF \
	DebugInfoBTF \
	Object \
	MC \
	MCParser \
	BinaryFormat \
	Option \
	TargetParser \
	BitReader \
	BitstreamReader \
	IR \
	Remarks \
	TextAPI \
	Support \
	Demangle

LLVM_DRIVER_TOOL=	llvm_objcopy
.include "${.PARSEDIR}/../../driver.mk"

.include "${.PARSEDIR}/../../link.mk"

LDADD+=	-lz
.if !defined(HOSTPROG)
DPADD+=	${LIBZ}
.endif

.include <bsd.prog.mk>
