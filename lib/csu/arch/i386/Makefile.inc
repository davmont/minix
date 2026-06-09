# $NetBSD: Makefile.inc,v 1.1 2010/08/07 18:01:33 joerg Exp $

CPPFLAGS+=	-DELFSIZE=32

# clang >= ~16 emits constructors into .init_array, not the legacy .ctors list.
# Process them in crt0 the modern way (see the amd64 Makefile.inc).
CPPFLAGS+=	-DHAVE_INITFINI_ARRAY


