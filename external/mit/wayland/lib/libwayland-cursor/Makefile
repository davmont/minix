#	MINIX: libwayland-cursor
#
# QtWayland links against this, so it is a hard prerequisite for Qt -- and so
# for anything above Qt.  It loads XCursor themes from disk and turns them into
# wl_buffers, which is why it needs the shm helpers in os-compatibility.c.
#
# It ships a built-in fallback cursor (cursor-data.h), so a system with no
# cursor theme installed still gets a pointer rather than nothing.

.include <bsd.init.mk>

LIB=		wayland-cursor

.PATH:	${WLSRCDIR}/cursor ${WLMINIXDIR}

SRCS=		wayland-cursor.c xcursor.c os-compatibility.c

.include "${WLDIR}/lib/wayland-gen.mk"

DPSRCS+=	${WL_VERSION_H} ${WL_CLIENT_H}

INCS=		wayland-cursor.h
INCSDIR=	/usr/include
.PATH:		${WLSRCDIR}/cursor

FILES+=			wayland-cursor.pc
FILESDIR_wayland-cursor.pc=	/usr/lib/pkgconfig
CLEANFILES+=		wayland-cursor.pc

wayland-cursor.pc: wayland-cursor.pc.in
	${_MKTARGET_CREATE}
	${TOOL_SED} -e 's/@VERSION@/${WAYLAND_VERSION}/g' \
		< ${.CURDIR}/wayland-cursor.pc.in > ${.TARGET}

all: ${WL_VERSION_H} ${WL_CLIENT_H} wayland-cursor.pc
dependall: all

.include <bsd.lib.mk>
