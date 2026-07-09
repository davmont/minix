# $NetBSD$
#
# libfbgui: minimal framebuffer graphics library (pixman + freetype +
# /dev/fb0 + /dev/mousemux).  See fbgui.h.

.include <bsd.own.mk>

LIB=		fbgui

SRCS=		fbgui.c

INCS=		fbgui.h
INCSDIR=	/usr/include

# pixman and freetype headers live in their own subdirectories.
CPPFLAGS+=	-I${DESTDIR}/usr/include/pixman-1
CPPFLAGS+=	-I${DESTDIR}/usr/include/freetype2

# The shared library links against pixman and freetype.
.if !defined(MLIBDIR)
LIBDPLIBS+=	freetype	${NETBSDSRCDIR}/external/mit/freetype/lib/libfreetype
LIBDPLIBS+=	pixman-1	${NETBSDSRCDIR}/external/mit/pixman/lib/libpixman
.endif

.include <bsd.lib.mk>
