/* MINIX: hand-written pixman configuration (pixman is built here from a
 * vendored source list rather than via meson).  Generic C path only - no
 * MMX/SSE2/SSSE3/NEON/VMX/MIPS/RVV fast paths - so none of the USE_* macros
 * are defined and the per-arch dispatch files compile to no-op stubs. */
#ifndef PIXMAN_CONFIG_H
#define PIXMAN_CONFIG_H

#define PACKAGE			"pixman"
#define PACKAGE_VERSION		"0.46.4"
#define PACKAGE_BUGREPORT	"pixman@lists.freedesktop.org"

/* Thread-local storage: MINIX/clang support __thread (amd64 TLS via
 * WRFSBASE).  pixman-compiler.h keys its TLS macro off "TLS". */
#define TLS			__thread

/* We do have working pthreads (libpthread); pixman only uses this for a
 * mutex fallback when TLS is unavailable, which is not our path. */
#define HAVE_PTHREADS		1

/* Endianness: amd64 is little-endian; leave WORDS_BIGENDIAN undefined. */

#endif /* PIXMAN_CONFIG_H */
