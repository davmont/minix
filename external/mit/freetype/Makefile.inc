#	MINIX: freetype reachover build glue.

.include <bsd.own.mk>

FREETYPEDIR=	${NETBSDSRCDIR}/external/mit/freetype
FREETYPESRCDIR=	${NETBSDSRCDIR}/external/mit/freetype/dist

.for _LIB in freetype
.if !defined(FREETYPEOBJDIR.${_LIB})
FREETYPEOBJDIR.${_LIB}!=	cd ${FREETYPEDIR}/lib/lib${_LIB} && ${PRINTOBJDIR}
.MAKEOVERRIDES+=	FREETYPEOBJDIR.${_LIB}
.endif
FREETYPELIB.${_LIB}=	${FREETYPEOBJDIR.${_LIB}}/lib${_LIB}.a
.endfor
