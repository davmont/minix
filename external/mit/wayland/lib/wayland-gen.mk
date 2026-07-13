#	MINIX: shared code generation for libwayland-server and -client.
#
# wayland-scanner turns protocol/wayland.xml into the marshalling tables and
# the protocol headers.  It is a BUILD-HOST tool -- it has to run during the
# cross build -- so it is compiled here with ${HOST_CC} against the host's
# expat, rather than for the target.  Building it from the vendored source
# (instead of using a wayland-scanner that happens to be installed on the
# developer's machine) keeps the build self-contained and reproducible.
#
# Every generated file is named by its absolute path.  Mixing relative
# prerequisites with an absolute target makes bmake treat them as unrelated
# nodes, and under -j the scanner then races ahead of the wayland-version.h it
# includes.

WL_XML=		${WLSRCDIR}/protocol/wayland.xml
WL_SCANNER=	${.OBJDIR}/wayland-scanner
WL_VERSION_H=	${.OBJDIR}/wayland-version.h
WL_PROTO_C=	${.OBJDIR}/wayland-protocol.c
WL_SERVER_H=	${.OBJDIR}/wayland-server-protocol.h
WL_CLIENT_H=	${.OBJDIR}/wayland-client-protocol.h

CPPFLAGS+=	-I${.OBJDIR}
CPPFLAGS+=	-I${WLMINIXDIR}/include	# config.h + the epoll/timerfd shim
CPPFLAGS+=	-I${WLSRCDIR}/src

# The vendored sources are not -Werror-clean under our warning set.
COPTS+=		-Wno-error
COPTS+=		-Wno-unknown-warning-option

CLEANFILES+=	${WL_VERSION_H} ${WL_SCANNER} ${WL_PROTO_C} \
		${WL_SERVER_H} ${WL_CLIENT_H}

${WL_VERSION_H}: ${WLSRCDIR}/src/wayland-version.h.in
	${_MKTARGET_CREATE}
	${TOOL_SED} -e 's/@WAYLAND_VERSION@/${WAYLAND_VERSION}/g' \
		    -e 's/@WAYLAND_VERSION_MAJOR@/${WAYLAND_VERSION_MAJOR}/g' \
		    -e 's/@WAYLAND_VERSION_MINOR@/${WAYLAND_VERSION_MINOR}/g' \
		    -e 's/@WAYLAND_VERSION_MICRO@/${WAYLAND_VERSION_MICRO}/g' \
		    < ${.ALLSRC} > ${.TARGET}

${WL_SCANNER}: ${WLSRCDIR}/src/scanner.c ${WLSRCDIR}/src/wayland-util.c \
		${WL_VERSION_H}
	${_MKTARGET_CREATE}
	${HOST_CC} -O2 -o ${.TARGET} \
		-I${.OBJDIR} -I${WLSRCDIR}/src \
		${WLSRCDIR}/src/scanner.c ${WLSRCDIR}/src/wayland-util.c \
		-lexpat

# public-code (not private-code): both libraries link these wl_interface
# tables and the symbols must be visible across them.
${WL_PROTO_C}: ${WL_SCANNER} ${WL_XML}
	${_MKTARGET_CREATE}
	${WL_SCANNER} public-code < ${WL_XML} > ${.TARGET}

${WL_SERVER_H}: ${WL_SCANNER} ${WL_XML}
	${_MKTARGET_CREATE}
	${WL_SCANNER} server-header < ${WL_XML} > ${.TARGET}

${WL_CLIENT_H}: ${WL_SCANNER} ${WL_XML}
	${_MKTARGET_CREATE}
	${WL_SCANNER} client-header < ${WL_XML} > ${.TARGET}
