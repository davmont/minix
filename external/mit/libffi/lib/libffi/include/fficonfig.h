/*	MINIX: fficonfig.h - hand-curated stand-in for autoconf's output.
 *
 * libffi normally generates this from fficonfig.h.in by running configure on
 * the target.  We do not run configure in a reachover build, so the answers
 * are recorded here.  Everything below is a statement about MINIX/x86; if a
 * new architecture is added to ../Makefile, revisit the sizes and the
 * assembler capabilities.
 */

#ifndef MINIX_FFICONFIG_H
#define MINIX_FFICONFIG_H

/* MINIX is ELF and has no leading underscore on C symbols. */
/* #undef SYMBOL_UNDERSCORE */

/* Not an Apple universal build; x86 is little-endian. */
/* #undef AC_APPLE_UNIVERSAL_BUILD */
/* #undef WORDS_BIGENDIAN */

/*
 * Closure trampolines.
 *
 * FFI_EXEC_TRAMPOLINE_TABLE is the iOS scheme (pre-allocated trampoline
 * pages); not applicable.
 *
 * FFI_EXEC_STATIC_TRAMP is libffi 3.4's memfd_create()-based scheme, which
 * maps one page of trampolines twice (once writable, once executable).  MINIX
 * has no memfd_create(), so it stays off and src/tramp.c compiles to stubs
 * (ffi_tramp_is_supported() returns 0).
 *
 * That leaves FFI_MMAP_EXEC_WRIT: allocate closure pages with
 * mmap(PROT_READ|PROT_WRITE|PROT_EXEC, MAP_ANON).  MINIX permits this (it
 * enforces no W^X), and the SELinux/PaX probes in closures.c are compiled out
 * because they are guarded on __linux__.
 *
 * Note that Wayland itself never allocates a closure -- libwayland only calls
 * ffi_call() -- so this path is built for completeness rather than exercised.
 */
/* #undef FFI_EXEC_TRAMPOLINE_TABLE */
/* #undef FFI_EXEC_STATIC_TRAMP */
/* #undef HAVE_MEMFD_CREATE */
/* #undef HAVE_SYS_MEMFD_H */
#define FFI_MMAP_EXEC_WRIT 1
/* #undef FFI_MMAP_EXEC_EMUTRAMP_PAX */

/* Keep the raw and struct APIs; libffi's defaults. */
/* #undef FFI_NO_RAW_API */
/* #undef FFI_NO_STRUCTS */
/* #undef FFI_DEBUG */
/* #undef USING_PURIFY */
/* #undef LIBFFI_GNU_SYMBOL_VERSIONING */

/*
 * Assembler capabilities.  Our assembler is clang's integrated one, which
 * supports CFI directives, x86 PC-relative operands, and the @unwind section
 * type used by src/x86/unix64.S.
 */
#define HAVE_AS_CFI_PSEUDO_OP 1
#define HAVE_AS_X86_PCREL 1
#define HAVE_AS_X86_64_UNWIND_SECTION_TYPE 1
/* #undef HAVE_AS_REGISTER_PSEUDO_OP */
/* #undef HAVE_AS_S390_ZARCH */
/* #undef HAVE_AS_SPARC_UA_PCREL */

/* ELF .eh_frame is read-only ("a"); it does not need to be writable. */
#define HAVE_RO_EH_FRAME 1
#define EH_FRAME_FLAGS "a"

#define HAVE_HIDDEN_VISIBILITY_ATTRIBUTE 1

/*
 * NetBSD (and thus MINIX) declares alloca() in <stdlib.h> and ships no
 * <alloca.h>; ffi_common.h falls back to that when HAVE_ALLOCA_H is unset.
 */
/* #undef HAVE_ALLOCA_H */

#define STDC_HEADERS 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#define HAVE_UNISTD_H 1
#define HAVE_DLFCN_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_MEMCPY 1

/* x86 has a genuine 80-bit long double (no IBM-style variant selection). */
#define HAVE_LONG_DOUBLE 1
/* #undef HAVE_LONG_DOUBLE_VARIANT */
/* #undef HAVE_PTRAUTH */

#define SIZEOF_DOUBLE 8
#if defined(__x86_64__) || defined(__amd64__)
# define SIZEOF_SIZE_T 8
# define SIZEOF_LONG_DOUBLE 16
#else
# define SIZEOF_SIZE_T 4
# define SIZEOF_LONG_DOUBLE 12
#endif

#define PACKAGE "libffi"
#define PACKAGE_NAME "libffi"
#define PACKAGE_TARNAME "libffi"
#define PACKAGE_VERSION "3.4.6"
#define PACKAGE_STRING "libffi 3.4.6"
#define PACKAGE_BUGREPORT "http://github.com/libffi/libffi/issues"
#define PACKAGE_URL ""
#define VERSION "3.4.6"
#define LT_OBJDIR ".libs/"

#ifdef HAVE_HIDDEN_VISIBILITY_ATTRIBUTE
# ifdef LIBFFI_ASM
#  define FFI_HIDDEN(name) .hidden name
# else
#  define FFI_HIDDEN __attribute__ ((visibility ("hidden")))
# endif
#else
# ifdef LIBFFI_ASM
#  define FFI_HIDDEN(name)
# else
#  define FFI_HIDDEN
# endif
#endif

#endif /* MINIX_FFICONFIG_H */
