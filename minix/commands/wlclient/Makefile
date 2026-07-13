#	MINIX: wlclient - a Wayland client, and the end-to-end check of the stack.
#
# Deliberately a separate binary from wlcompd.  MINIX links statically, and
# libwayland-server and libwayland-client each carry a copy of the generated
# wl_interface tables, so one program cannot have both.

.include <bsd.own.mk>
.include "${NETBSDSRCDIR}/external/mit/wayland/Makefile.inc"

PROG=	wlclient
MAN=

SRCS=	wlclient.c xdg-shell-protocol.c

CPPFLAGS+=	-I${.OBJDIR}

LDADD+=	-lwayland-client -lffi -lpthread -lm
DPADD+=	${DESTDIR}/usr/lib/libwayland-client.a ${DESTDIR}/usr/lib/libffi.a

XDG_XML=	${WLDIR}/protocols/xdg-shell/xdg-shell.xml
WL_SCANNER=	${.OBJDIR}/wayland-scanner

CLEANFILES+=	wayland-scanner wayland-version.h \
		xdg-shell-protocol.c xdg-shell-client-protocol.h

wayland-version.h: ${WLSRCDIR}/src/wayland-version.h.in
	${_MKTARGET_CREATE}
	${TOOL_SED} -e 's/@WAYLAND_VERSION@/${WAYLAND_VERSION}/g' \
		    -e 's/@WAYLAND_VERSION_MAJOR@/${WAYLAND_VERSION_MAJOR}/g' \
		    -e 's/@WAYLAND_VERSION_MINOR@/${WAYLAND_VERSION_MINOR}/g' \
		    -e 's/@WAYLAND_VERSION_MICRO@/${WAYLAND_VERSION_MICRO}/g' \
		    < ${.ALLSRC} > ${.TARGET}

wayland-scanner: ${WLSRCDIR}/src/scanner.c ${WLSRCDIR}/src/wayland-util.c \
		wayland-version.h
	${_MKTARGET_CREATE}
	${HOST_CC} -O2 -o ${.TARGET} \
		-I${.OBJDIR} -I${WLSRCDIR}/src \
		${WLSRCDIR}/src/scanner.c ${WLSRCDIR}/src/wayland-util.c \
		-lexpat

xdg-shell-protocol.c: wayland-scanner ${XDG_XML}
	${_MKTARGET_CREATE}
	${WL_SCANNER} private-code < ${XDG_XML} > ${.TARGET}

xdg-shell-client-protocol.h: wayland-scanner ${XDG_XML}
	${_MKTARGET_CREATE}
	${WL_SCANNER} client-header < ${XDG_XML} > ${.TARGET}

DPSRCS+=	xdg-shell-protocol.c xdg-shell-client-protocol.h

.include <bsd.prog.mk>

${OBJS}: xdg-shell-client-protocol.h
.for _f in ${SRCS:M*.c:T:R}
${_f}.d: xdg-shell-client-protocol.h
.endfor
