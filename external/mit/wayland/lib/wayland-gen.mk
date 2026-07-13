#	MINIX: shared code generation for libwayland-server and -client.
#
# wayland-scanner turns protocol/wayland.xml into the marshalling tables and
# the protocol headers.  It is a BUILD-HOST tool -- it has to run during the
# cross build -- so it is compiled here with ${HOST_CC} against the host's
# expat, rather than for the target.  Building it from the vendored source
# (instead of using a wayland-scanner that happens to be installed on the
# developer's machine) keeps the build self-contained and reproducible.

WL_XML=		${WLSRCDIR}/protocol/wayland.xml
WL_SCANNER=	${.OBJDIR}/wayland-scanner

CPPFLAGS+=	-I${.OBJDIR}
CPPFLAGS+=	-I${WLMINIXDIR}/include	# config.h + the epoll/timerfd shim
CPPFLAGS+=	-I${WLSRCDIR}/src

# The vendored sources are not -Werror-clean under our warning set.
COPTS+=		-Wno-error
COPTS+=		-Wno-unknown-warning-option

CLEANFILES+=	wayland-version.h wayland-scanner \
		wayland-protocol.c \
		wayland-server-protocol.h wayland-client-protocol.h

wayland-version.h: ${WLSRCDIR}/src/wayland-version.h.in
	${_MKTARGET_CREATE}
	${TOOL_SED} -e 's/@WAYLAND_VERSION@/${WAYLAND_VERSION}/g' \
		    -e 's/@WAYLAND_VERSION_MAJOR@/${WAYLAND_VERSION_MAJOR}/g' \
		    -e 's/@WAYLAND_VERSION_MINOR@/${WAYLAND_VERSION_MINOR}/g' \
		    -e 's/@WAYLAND_VERSION_MICRO@/${WAYLAND_VERSION_MICRO}/g' \
		    < ${.ALLSRC} > ${.TARGET}

${WL_SCANNER}: ${WLSRCDIR}/src/scanner.c ${WLSRCDIR}/src/wayland-util.c \
		wayland-version.h
	${_MKTARGET_CREATE}
	${HOST_CC} -O2 -o ${.TARGET} \
		-I${.OBJDIR} -I${WLSRCDIR}/src \
		${WLSRCDIR}/src/scanner.c ${WLSRCDIR}/src/wayland-util.c \
		-lexpat

# public-code (not private-code): both libraries link these wl_interface
# tables and the symbols must be visible across them.
wayland-protocol.c: ${WL_SCANNER} ${WL_XML}
	${_MKTARGET_CREATE}
	${WL_SCANNER} public-code < ${WL_XML} > ${.TARGET}

wayland-server-protocol.h: ${WL_SCANNER} ${WL_XML}
	${_MKTARGET_CREATE}
	${WL_SCANNER} server-header < ${WL_XML} > ${.TARGET}

wayland-client-protocol.h: ${WL_SCANNER} ${WL_XML}
	${_MKTARGET_CREATE}
	${WL_SCANNER} client-header < ${WL_XML} > ${.TARGET}
