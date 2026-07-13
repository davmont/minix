#	MINIX: libxkbcommon reachover build glue.

.include <bsd.own.mk>

XKBDIR=		${NETBSDSRCDIR}/external/mit/libxkbcommon
XKBSRCDIR=	${NETBSDSRCDIR}/external/mit/libxkbcommon/dist
XKBMINIXDIR=	${NETBSDSRCDIR}/external/mit/libxkbcommon/minix

XKB_VERSION=	1.7.0

.for _LIB in xkbcommon
.if !defined(XKBOBJDIR.${_LIB})
XKBOBJDIR.${_LIB}!=	cd ${XKBDIR}/lib/lib${_LIB} && ${PRINTOBJDIR}
.MAKEOVERRIDES+=	XKBOBJDIR.${_LIB}
.endif
XKBLIB.${_LIB}=	${XKBOBJDIR.${_LIB}}/lib${_LIB}.a
.endfor
