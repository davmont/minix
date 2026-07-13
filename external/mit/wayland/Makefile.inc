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
