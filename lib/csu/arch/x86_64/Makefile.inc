# $NetBSD: Makefile.inc,v 1.1 2010/08/07 18:01:34 joerg Exp $

CPPFLAGS+=	-DELFSIZE=64

# clang >= ~16 (MINIX/amd64 is built with clang 22) emits C/C++ constructors
# into .init_array, not the legacy .ctors list.  Process them in crt0 the modern
# way so __attribute__((constructor)) functions (e.g. libc's __minix_init) run.
CPPFLAGS+=	-DHAVE_INITFINI_ARRAY
