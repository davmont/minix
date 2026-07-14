#	$NetBSD: Makefile.inc,v 1.8 2010/05/30 08:28:53 tnozaki Exp $

# sources
.PATH: ${ARCHDIR}/citrus ${.CURDIR}/citrus ${.CURDIR}/citrus/modules

.if !defined(__MINIX)
MODULE_SHLIB_MAJOR!= . ${NETBSDSRCDIR}/lib/i18n_module/shlib_version ; echo $$major
.else
# MINIX cannot run the shell-sourcing .for above during the cross-build, so the
# value is hardcoded here.  It MUST match major= in lib/i18n_module/shlib_version,
# which is what the i18n modules (e.g. libUTF8.so.5.0) are actually installed as.
# citrus _findshlib() only matches a module whose major equals I18NMODULE_MAJOR;
# a stale value (was 1, modules are 5) makes setlocale(LC_CTYPE,"*.UTF-8") fail
# with EINVAL for every dynamically-linked program.
MODULE_SHLIB_MAJOR=5
.endif # !defined(__MINIX)
CPPFLAGS.citrus_module.c+=-DI18NMODULE_MAJOR=${MODULE_SHLIB_MAJOR}
SRCS+=	citrus_bcs.c citrus_bcs_strtol.c citrus_bcs_strtoul.c \
	citrus_csmapper.c citrus_ctype.c citrus_ctype_fallback.c \
	citrus_db.c citrus_db_hash.c citrus_esdb.c citrus_hash.c \
	citrus_iconv.c citrus_lookup.c \
	citrus_mapper.c citrus_memstream.c citrus_mmap.c citrus_module.c \
	citrus_none.c citrus_stdenc.c

# citrus loads the LC_CTYPE encodings and the iconv converters as modules
# dlopen'd from /usr/lib/i18n.  MINIX links its programs statically -- /bin/sh,
# /bin/ls and init all are -- and a static program cannot dlopen, so none of
# those modules could ever be loaded: no locale but C, and no iconv at all.
# Compile the ones that matter into libc instead; citrus_module.c looks them up
# in a table before reaching for dlopen.  See citrus_module_builtin.c.
#
# Only multibyte encodings need a module here (UTF8); single-byte ones already
# use citrus_none.c above.  The legacy CJK encodings are left out on purpose --
# a lot of code for something the base system never asks for, and a dynamically
# linked program can still dlopen them.
SRCS+=	citrus_module_builtin.c \
	citrus_utf8.c \
	citrus_iconv_std.c citrus_iconv_none.c \
	citrus_mapper_std.c citrus_mapper_none.c citrus_mapper_646.c \
	citrus_mapper_serial.c citrus_mapper_zone.c

# The module sources live in citrus/modules but include citrus/ headers by plain
# "..." quotes, which resolve relative to the including file's directory -- and
# citrus_module_builtin.c, which sits in citrus/, includes the module headers.
# So both directories have to be on the include path.
CPPFLAGS+=	-I${.CURDIR}/citrus -I${.CURDIR}/citrus/modules
SRCS+=	citrus_lc_ctype.c \
	citrus_lc_monetary.c \
	citrus_lc_numeric.c \
	citrus_lc_time.c \
	citrus_lc_messages.c
CPPFLAGS.citrus_bcs_strtol.c+=	-I${LIBCDIR}/../../common/lib/libc/stdlib
CPPFLAGS.citrus_bcs_strtoul.c+=	-I${LIBCDIR}/../../common/lib/libc/stdlib
CPPFLAGS.citrus_lc_ctype.c+=	-I${LIBCDIR}/locale
CPPFLAGS.citrus_lc_monetary.c+=	-I${LIBCDIR}/locale
CPPFLAGS.citrus_lc_numeric.c+=	-I${LIBCDIR}/locale
CPPFLAGS.citrus_lc_time.c+=	-I${LIBCDIR}/locale
CPPFLAGS.citrus_lc_messages.c+=	-I${LIBCDIR}/locale
