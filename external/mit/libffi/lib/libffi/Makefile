#	MINIX: libffi - foreign function interface.
#
# libwayland's generated marshalling code dispatches protocol requests and
# events through ffi_call(), which is why libffi is a hard prerequisite for
# Wayland.  Only ffi_call() is on that path (wl_closure_invoke); the closure
# side (ffi_prep_closure) is built but unused by Wayland.
#
# libffi is normally configured by autoconf, which we do not run.  ffi.h is
# generated here from the upstream ffi.h.in template, and fficonfig.h is
# curated by hand in include/ (see the comments there).

.include <bsd.init.mk>

LIB=		ffi

.PATH:	${FFISRCDIR}/src

# Architecture-independent core.
SRCS=		prep_cif.c types.c raw_api.c java_raw_api.c closures.c tramp.c

# Per-architecture back end.  TARGET is substituted into ffi.h (it selects the
# ABI enum in ffitarget.h), and the long double size must match the ABI.
.if ${MACHINE_ARCH} == "x86_64"
.PATH:		${FFISRCDIR}/src/x86
# ffi64.c/unix64.S implement the SysV AMD64 ABI; ffiw64.c/win64.S implement
# FFI_GNUW64, which upstream also builds on every x86_64 host.
SRCS+=		ffi64.c unix64.S ffiw64.c win64.S
FFI_TARGET=	X86_64
FFI_ARCHDIR=	x86
.elif ${MACHINE_ARCH} == "i386"
.PATH:		${FFISRCDIR}/src/x86
SRCS+=		ffi.c sysv.S
FFI_TARGET=	X86
FFI_ARCHDIR=	x86
.else
.error libffi: unsupported MACHINE_ARCH ${MACHINE_ARCH} (add its back end here)
.endif

FFI_VERSION=	3.4.6

# fficonfig.h (curated, here) must win over anything in dist; ffitarget.h is
# taken from the arch back end, and the generated ffi.h lands in ${.OBJDIR}.
CPPFLAGS+=	-I${.CURDIR}/include
CPPFLAGS+=	-I${.OBJDIR}
CPPFLAGS+=	-I${FFISRCDIR}/include
CPPFLAGS+=	-I${FFISRCDIR}/src/${FFI_ARCHDIR}

# The vendored sources are not -Werror-clean under our warning set.
COPTS+=		-Wno-error
COPTS+=		-Wno-unknown-warning-option

INCS=		ffi.h ffitarget.h
INCSDIR=	/usr/include
.PATH:		${FFISRCDIR}/src/${FFI_ARCHDIR}		# ffitarget.h

DPSRCS+=	ffi.h
CLEANFILES+=	ffi.h

# Substitutions performed by autoconf's AC_CONFIG_FILES on ffi.h.in.
# FFI_EXEC_TRAMPOLINE_TABLE is 0: that path is iOS-only (pre-allocated
# trampoline pages).  MINIX uses the mmap(PROT_EXEC) closure path instead.
ffi.h: ${FFISRCDIR}/include/ffi.h.in
	${_MKTARGET_CREATE}
	${TOOL_SED} -e 's/@VERSION@/${FFI_VERSION}/g' \
		    -e 's/@TARGET@/${FFI_TARGET}/g' \
		    -e 's/@HAVE_LONG_DOUBLE@/1/g' \
		    -e 's/@FFI_EXEC_TRAMPOLINE_TABLE@/0/g' \
		    < ${.ALLSRC} > ${.TARGET}

FILES+=			libffi.pc
FILESDIR_libffi.pc=	/usr/lib/pkgconfig
CLEANFILES+=		libffi.pc

libffi.pc: libffi.pc.in
	${_MKTARGET_CREATE}
	${TOOL_SED} -e 's/@VERSION@/${FFI_VERSION}/g' \
		< ${.CURDIR}/libffi.pc.in > ${.TARGET}

all: ffi.h libffi.pc
dependall: all

.include <bsd.lib.mk>
