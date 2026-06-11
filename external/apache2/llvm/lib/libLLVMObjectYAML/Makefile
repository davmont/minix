#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:53 joerg Exp $

LIB=	LLVMObjectYAML

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/lib/ObjectYAML

SRCS+=	ArchiveEmitter.cpp \
	ArchiveYAML.cpp \
	COFFEmitter.cpp \
	COFFYAML.cpp \
	CodeViewYAMLDebugSections.cpp \
	CodeViewYAMLSymbols.cpp \
	CodeViewYAMLTypeHashing.cpp \
	CodeViewYAMLTypes.cpp \
	DWARFEmitter.cpp \
	DWARFYAML.cpp \
	DXContainerEmitter.cpp \
	DXContainerYAML.cpp \
	ELFEmitter.cpp \
	ELFYAML.cpp \
	GOFFEmitter.cpp \
	GOFFYAML.cpp \
	MachOEmitter.cpp \
	MachOYAML.cpp \
	MinidumpEmitter.cpp \
	MinidumpYAML.cpp \
	ObjectYAML.cpp \
	OffloadEmitter.cpp \
	OffloadYAML.cpp \
	WasmEmitter.cpp \
	WasmYAML.cpp \
	XCOFFEmitter.cpp \
	XCOFFYAML.cpp \
	YAML.cpp \
	yaml2obj.cpp

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
