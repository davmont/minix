# $NetBSD$
#
# fbcompd: the framebuffer compositor.  Owns /dev/fb0 (libfbgui) and
# serves window clients over an AF_UNIX socket + SysV shm.  See
# fbcomp_proto.h (libfbclient) for the protocol.

.include <bsd.own.mk>

PROG=	fbcompd
MAN=

# pixman and freetype headers live in their own subdirectories.
CPPFLAGS+=	-I${DESTDIR}/usr/include/pixman-1
CPPFLAGS+=	-I${DESTDIR}/usr/include/freetype2

LDADD+=	-lfbgui -lfreetype -lpixman-1 -lm
DPADD+=	${DESTDIR}/usr/lib/libfbgui.a ${DESTDIR}/usr/lib/libfreetype.a \
	${DESTDIR}/usr/lib/libpixman-1.a

.include <bsd.prog.mk>
