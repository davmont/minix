#	MINIX: wlcompd - the Wayland compositor.
#
# Owns /dev/fb0 through libfbgui (pixman + FreeType) and speaks Wayland to its
# clients.  xdg_shell is not part of wayland core, so its marshalling code is
# generated here from the vendored protocol XML with wayland-scanner, which --
# being a build-host tool -- is compiled with ${HOST_CC} against the host's
# expat, exactly as libwayland does it.

.include <bsd.own.mk>
.include "${NETBSDSRCDIR}/external/mit/wayland/Makefile.inc"

PROG=	wlcompd
MAN=

SRCS=	wlcompd.c xdg-shell-protocol.c wlr-layer-shell-protocol.c

# pixman and freetype headers live in their own subdirectories.
CPPFLAGS+=	-I${DESTDIR}/usr/include/pixman-1
CPPFLAGS+=	-I${DESTDIR}/usr/include/freetype2
CPPFLAGS+=	-I${.OBJDIR}

LDADD+=	-lwayland-server -lffi -lxkbcommon -lfbgui -lfreetype -lpixman-1 \
	-lpthread -lm
DPADD+=	${DESTDIR}/usr/lib/libwayland-server.a ${DESTDIR}/usr/lib/libffi.a \
	${DESTDIR}/usr/lib/libxkbcommon.a ${DESTDIR}/usr/lib/libfbgui.a \
	${DESTDIR}/usr/lib/libfreetype.a ${DESTDIR}/usr/lib/libpixman-1.a

XDG_XML=	${WLDIR}/protocols/xdg-shell/xdg-shell.xml
# wlr-layer-shell: how a panel or a dock anchors itself to an edge of the
# screen.  lxqt-panel (and every other Wayland panel) needs it.
LAYER_XML=	${WLDIR}/protocols/wlr-layer-shell/wlr-layer-shell-unstable-v1.xml
WL_SCANNER=	${.OBJDIR}/wayland-scanner

CLEANFILES+=	wayland-scanner wayland-version.h \
		xdg-shell-protocol.c xdg-shell-server-protocol.h \
		wlr-layer-shell-protocol.c wlr-layer-shell-server-protocol.h

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

xdg-shell-server-protocol.h: wayland-scanner ${XDG_XML}
	${_MKTARGET_CREATE}
	${WL_SCANNER} server-header < ${XDG_XML} > ${.TARGET}

wlr-layer-shell-protocol.c: wayland-scanner ${LAYER_XML}
	${_MKTARGET_CREATE}
	${WL_SCANNER} private-code < ${LAYER_XML} > ${.TARGET}

wlr-layer-shell-server-protocol.h: wayland-scanner ${LAYER_XML}
	${_MKTARGET_CREATE}
	${WL_SCANNER} server-header < ${LAYER_XML} > ${.TARGET}

DPSRCS+=	xdg-shell-protocol.c xdg-shell-server-protocol.h \
		wlr-layer-shell-protocol.c wlr-layer-shell-server-protocol.h

.include <bsd.prog.mk>

# Every object needs the generated protocol header; DPSRCS does not order that
# under -j, and OBJS only exists once bsd.prog.mk has been read.
${OBJS}: xdg-shell-server-protocol.h wlr-layer-shell-server-protocol.h
.for _f in ${SRCS:M*.c:T:R}
${_f}.d: xdg-shell-server-protocol.h wlr-layer-shell-server-protocol.h
.endfor
