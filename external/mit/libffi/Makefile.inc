#	MINIX: libffi reachover build glue.

.include <bsd.own.mk>

FFIDIR=		${NETBSDSRCDIR}/external/mit/libffi
FFISRCDIR=	${NETBSDSRCDIR}/external/mit/libffi/dist

.for _LIB in ffi
.if !defined(FFIOBJDIR.${_LIB})
FFIOBJDIR.${_LIB}!=	cd ${FFIDIR}/lib/lib${_LIB} && ${PRINTOBJDIR}
.MAKEOVERRIDES+=	FFIOBJDIR.${_LIB}
.endif
FFILIB.${_LIB}=	${FFIOBJDIR.${_LIB}}/lib${_LIB}.a
.endfor
