#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:58 joerg Exp $

LIB=	clangToolingRefactoring

.include <bsd.init.mk>

.PATH: ${CLANG_SRCDIR}/lib/Tooling/Refactoring
.PATH: ${CLANG_SRCDIR}/lib/Tooling/Refactoring/Extract
.PATH: ${CLANG_SRCDIR}/lib/Tooling/Refactoring/Rename

SRCS+=	ASTSelection.cpp \
	ASTSelectionRequirements.cpp \
	AtomicChange.cpp \
	Extract.cpp \
	Lookup.cpp \
	RefactoringActions.cpp \
	RenamingAction.cpp \
	SourceExtraction.cpp \
	SymbolOccurrences.cpp \
	USRFinder.cpp \
	USRFindingAction.cpp \
	USRLocFinder.cpp





.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
