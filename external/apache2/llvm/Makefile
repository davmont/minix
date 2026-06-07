#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:45 joerg Exp $

.include <bsd.init.mk>

SUBDIR=	include \

.if ${MKLLVMRT} != "no"
SUBDIR+= \
	librt
.endif

# Target-side LLVM/clang (clang running ON MINIX) is gated on MKLLVMCMDS, as the
# old external/bsd/llvm was.  The host cross-clang (tools/llvm*) is what builds
# the system; this is the optional on-target compiler.
.if ${MKLLVM} != "no" && ${MKLLVMCMDS:Uyes} != "no"
SUBDIR+= \
	lib \
	.WAIT \
	bin
.endif

prepare-import:
	cd ${LLVM_TOPLEVEL}/dist && rm -rf */test */unittests */www

.include "Makefile.inc"

.include <bsd.subdir.mk>
