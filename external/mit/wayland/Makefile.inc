#	MINIX: wayland reachover build glue.

.include <bsd.own.mk>

WLDIR=		${NETBSDSRCDIR}/external/mit/wayland
WLSRCDIR=	${NETBSDSRCDIR}/external/mit/wayland/dist
WLMINIXDIR=	${NETBSDSRCDIR}/external/mit/wayland/minix

WAYLAND_VERSION=	1.22.0
WAYLAND_VERSION_MAJOR=	1
WAYLAND_VERSION_MINOR=	22
WAYLAND_VERSION_MICRO=	0

.for _LIB in wayland-server wayland-client
.if !defined(WLOBJDIR.${_LIB})
WLOBJDIR.${_LIB}!=	cd ${WLDIR}/lib/lib${_LIB} && ${PRINTOBJDIR}
.MAKEOVERRIDES+=	WLOBJDIR.${_LIB}
.endif
WLLIB.${_LIB}=	${WLOBJDIR.${_LIB}}/lib${_LIB}.a
.endfor

# libffi and libxkbcommon are hard prerequisites of the Wayland stack (ffi_call
# in the marshalling path; the keymap compiler).  Expose their build-tree
# objdirs so a consumer can link them with -L<objdir> before they are installed
# into ${DESTDIR} -- a clean or an interrupted "-u" world build reaches these
# programs while the libraries are still only in their objdirs.
FFILIBSRCDIR=	${NETBSDSRCDIR}/external/mit/libffi/lib/libffi
XKBLIBSRCDIR=	${NETBSDSRCDIR}/external/mit/libxkbcommon/lib/libxkbcommon
.if !defined(FFIOBJDIR)
FFIOBJDIR!=	cd ${FFILIBSRCDIR} && ${PRINTOBJDIR}
.MAKEOVERRIDES+=	FFIOBJDIR
.endif
.if !defined(XKBOBJDIR)
XKBOBJDIR!=	cd ${XKBLIBSRCDIR} && ${PRINTOBJDIR}
.MAKEOVERRIDES+=	XKBOBJDIR
.endif
WLLIB.ffi=	${FFIOBJDIR}/libffi.a
WLLIB.xkbcommon=	${XKBOBJDIR}/libxkbcommon.a
