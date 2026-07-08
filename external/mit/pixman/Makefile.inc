#	MINIX: pixman reachover build glue.

.include <bsd.own.mk>

PIXMANDIR=	${NETBSDSRCDIR}/external/mit/pixman
PIXMANSRCDIR=	${NETBSDSRCDIR}/external/mit/pixman/dist

.for _LIB in pixman
.if !defined(PIXMANOBJDIR.${_LIB})
PIXMANOBJDIR.${_LIB}!=	cd ${PIXMANDIR}/lib/lib${_LIB} && ${PRINTOBJDIR}
.MAKEOVERRIDES+=	PIXMANOBJDIR.${_LIB}
.endif
PIXMANLIB.${_LIB}=	${PIXMANOBJDIR.${_LIB}}/lib${_LIB}.a
.endfor
