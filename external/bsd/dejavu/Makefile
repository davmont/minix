# $NetBSD$
#
# MINIX: DejaVu TrueType fonts (Bitstream Vera Fonts license, a permissive
# MIT-style license; DejaVu changes are public domain).  Installed so the
# framebuffer compositor (/usr/bin/fbcompd) and any other TTF consumer has
# a usable font on a base system - the tree ships no other TTF since the
# X11 fonts were removed.

.include <bsd.own.mk>

.PATH:		${.CURDIR}/dist

# The scalable fonts.
FILES=			DejaVuSansMono.ttf DejaVuSans.ttf
FILESDIR=		/usr/share/fonts/TTF

# The license text alongside them.
FILES+=			LICENSE
FILESDIR_LICENSE=	/usr/share/doc/fonts/dejavu

.include <bsd.prog.mk>
