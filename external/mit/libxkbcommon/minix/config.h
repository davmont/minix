/*	MINIX: stand-in for the config.h that libxkbcommon's meson build generates.
 *
 * Each answer was checked against the tree rather than assumed.
 */

#ifndef MINIX_XKB_CONFIG_H
#define MINIX_XKB_CONFIG_H

/* NetBSD libc (and thus MINIX) has all of these. */
#define HAVE_ASPRINTF 1
#define HAVE_VASPRINTF 1
#define HAVE_STRNDUP 1
#define HAVE_MMAP 1
#define HAVE_UNISTD_H 1

/* clang provides it; xkbcommon only uses it for likely()/unlikely(). */
#define HAVE___BUILTIN_EXPECT 1

/*
 * Absent on MINIX:
 *   eaccess()/euidaccess()  -- xkbcommon falls back to access(2).
 *   secure_getenv()         -- a glibc extension.  xkbcommon's fallback is
 *                              getenv() guarded by issetugid(), which MINIX
 *                              does have, so privilege-dropping still works.
 */
/* #undef HAVE_EACCESS */
/* #undef HAVE_EUIDACCESS */
/* #undef HAVE_SECURE_GETENV */
/* #undef HAVE___SECURE_GETENV */

#endif /* MINIX_XKB_CONFIG_H */
