#	$NetBSD: bsd.host.mk,v 1.2 2014/04/10 19:02:18 plunky Exp $

.if !defined(_BSD_HOST_MK_)
_BSD_HOST_MK_=1

.if ${HOST_OSTYPE:C/\-.*//:U} == "Minix"
HOST_LDFLAGS?=	-static

#LSC: Be a bit smarter about the default compiler
.if exists(/usr/pkg/bin/clang) || exists(/usr/bin/clang)
HOST_CC?=   clang
.endif

.if exists(/usr/pkg/bin/gcc) || exists(/usr/bin/gcc)
HOST_CC?=   gcc
.endif
.endif # ${HOST_OSTYPE:C/\-.*//:U} == "Minix"

# On Linux, prefer clang++ over g++ to avoid GCC 15 / glibc 2.41+ header
# incompatibilities in ext/concurrence.h that break old C++11 LLVM code.
# Also disable PIE for host tools: old LLVM/binutils objects use absolute
# 32-bit relocations and cannot be linked as position-independent executables.
.if ${HOST_OSTYPE:C/\-.*//:U} == "Linux"
.if exists(/usr/bin/clang++)
HOST_CXX?=	clang++
.elif exists(/usr/local/bin/clang++)
HOST_CXX?=	clang++
.endif
HOST_LDFLAGS?=	-no-pie
.endif # ${HOST_OSTYPE:C/\-.*//:U} == "Linux"

# Helpers for cross-compiling
HOST_CC?=	cc
HOST_CFLAGS?=	-O
HOST_COMPILE.c?=${HOST_CC} ${HOST_CFLAGS} ${HOST_CPPFLAGS} -c
HOST_COMPILE.cc?=      ${HOST_CXX} ${HOST_CXXFLAGS} ${HOST_CPPFLAGS} -c
HOST_LINK.cc?=  ${HOST_CXX} ${HOST_CXXFLAGS} ${HOST_CPPFLAGS} ${HOST_LDFLAGS}
.if defined(HOSTPROG_CXX)
HOST_LINK.c?=   ${HOST_LINK.cc}
.else
HOST_LINK.c?=	${HOST_CC} ${HOST_CFLAGS} ${HOST_CPPFLAGS} ${HOST_LDFLAGS}
.endif

HOST_CXX?=	c++
HOST_CXXFLAGS?=	-O

HOST_CPP?=	cpp
HOST_CPPFLAGS?=

HOST_LD?=	ld
HOST_LDFLAGS?=

HOST_AR?=	ar
HOST_RANLIB?=	ranlib

HOST_LN?=	ln

# HOST_SH must be an absolute path
HOST_SH?=	/bin/sh

.if !defined(HOST_OSTYPE)
_HOST_OSNAME!=	uname -s
_HOST_OSREL!=	uname -r
# For _HOST_ARCH, if uname -p fails, or prints "unknown", or prints
# something that does not look like an identifier, then use uname -m.
_HOST_ARCH!=	uname -p 2>/dev/null
_HOST_ARCH:=	${HOST_ARCH:tW:C/.*[^-_A-Za-z0-9].*//:S/unknown//}
.if empty(_HOST_ARCH)
_HOST_ARCH!=	uname -m
.endif
HOST_OSTYPE:=	${_HOST_OSNAME}-${_HOST_OSREL:C/\([^\)]*\)//g:[*]:C/ /_/g}-${_HOST_ARCH:C/\([^\)]*\)//g:[*]:C/ /_/g}
.MAKEOVERRIDES+= HOST_OSTYPE
.endif # !defined(HOST_OSTYPE)

#
# TOOLDIR_OSTYPE: the key used to name TOOLDIR (see <bsd.own.mk>).
#
# The contents of TOOLDIR are *host* executables, so what decides whether they
# can be reused is the host OS and architecture - not the host *kernel* release.
#
# HOST_OSTYPE embeds `uname -r`, which is right on NetBSD/Minix, where the
# release identifies the whole OS (kernel *and* userland, so libc moves with
# it).  On Linux it identifies only the kernel: it changes with every routine
# kernel package update, while the userspace ABI Linux guarantees keeps the
# existing host tools working perfectly.  Keying TOOLDIR on it there means an
# unrelated system update silently invalidates the toolchain and forces a
# multi-hour rebuild of the tools (and, through them, the target LLVM).
#
# So on Linux hosts, key TOOLDIR on OS and architecture only.  Everywhere else
# the key is unchanged.  Derived from HOST_OSTYPE so it is still correct when
# HOST_OSTYPE is supplied from the environment.
#
.if !defined(TOOLDIR_OSTYPE)
.if ${HOST_OSTYPE:C/\-.*//} == "Linux"
TOOLDIR_OSTYPE:=	${HOST_OSTYPE:C/\-.*//}-${HOST_OSTYPE:C/.*\-//}
.else
TOOLDIR_OSTYPE:=	${HOST_OSTYPE}
.endif
.MAKEOVERRIDES+= TOOLDIR_OSTYPE
.endif # !defined(TOOLDIR_OSTYPE)

.if ${USETOOLS} == "yes"
HOST_MKDEP?=	${TOOLDIR}/bin/${_TOOL_PREFIX}host-mkdep
HOST_MKDEPCXX?=	${TOOLDIR}/bin/${_TOOL_PREFIX}host-mkdep
.else
HOST_MKDEP?=	CC=${HOST_CC:Q} mkdep
HOST_MKDEPCXX?=	CC=${HOST_CXX:Q} mkdep
.endif

.if ${NEED_OWN_INSTALL_TARGET} != "no"
HOST_INSTALL_FILE?=	${INSTALL} ${COPY} ${PRESERVE} ${RENAME}
HOST_INSTALL_DIR?=	${INSTALL} -d
HOST_INSTALL_SYMLINK?=	${INSTALL} ${SYMLINK} ${RENAME}
.endif

.endif
