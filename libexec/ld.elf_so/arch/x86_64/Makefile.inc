#	$NetBSD: Makefile.inc,v 1.7 2005/06/04 16:17:17 lukem Exp $

SRCS+=		rtld_start.S mdreloc.c

# XXX Should not be in CPPFLAGS!
CPPFLAGS+=	-fPIC

CPPFLAGS+=	-DELFSIZE=64
# Call DT_INIT_ARRAY/DT_FINI_ARRAY functions of shared objects (rtld.c guards
# the calling loops with this).  clang (unlike the old in-tree gcc) emits
# constructors into .init_array, so without this the C library's own
# constructors -- notably __minix_init(), which fetches the kernel info page --
# never run in dynamically linked programs: _minix_kerninfo stays NULL and the
# process crashes on first use (e.g. execvp() after fork, or early daemon
# startup).  aarch64 already had this; x86_64 was missed.
CPPFLAGS+=	-DHAVE_INITFINI_ARRAY

LDFLAGS+=	-Wl,-e,.rtld_start
