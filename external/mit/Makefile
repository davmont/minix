#	$NetBSD: Makefile,v 1.5 2015/09/03 22:24:02 youri Exp $

.include <bsd.own.mk>

# libxkbcommon and wayland link against libffi (wayland's generated marshalling
# goes through ffi_call), so libffi has to be built AND installed first.  Without
# the .WAIT bmake runs these in parallel under -j and the consumers reach the
# link with no libffi in DESTDIR yet: "unable to find library -lffi".
SUBDIR+=	expat lua pixman freetype libffi .WAIT libxkbcommon wayland

.include <bsd.subdir.mk>
