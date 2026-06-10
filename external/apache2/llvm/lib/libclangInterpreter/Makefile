#	$NetBSD: Makefile,v 1.1 2021/05/30 01:56:57 joerg Exp $

LIB=	clangInterpreter

.include <bsd.init.mk>

.PATH: ${CLANG_SRCDIR}/lib/Interpreter

CPPFLAGS+=	-I${CLANG_SRCDIR}/lib/Interpreter

SRCS+=	CodeCompletion.cpp \
	DeviceOffload.cpp \
	IncrementalAction.cpp \
	IncrementalExecutor.cpp \
	IncrementalParser.cpp \
	Interpreter.cpp \
	InterpreterUtils.cpp \
	InterpreterValuePrinter.cpp \
	OrcIncrementalExecutor.cpp \
	Value.cpp

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
