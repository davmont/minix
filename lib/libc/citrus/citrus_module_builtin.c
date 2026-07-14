/*	$MINIX$	*/

/*-
 * Built-in citrus modules, for programs that cannot dlopen.
 *
 * citrus loads both the LC_CTYPE encoding handlers and the iconv converters as
 * shared modules dlopen'd out of /usr/lib/i18n.  MINIX links its programs
 * statically -- /bin/sh, /bin/ls and init all are, and Qt has to be, because a
 * dynamically linked Qt segfaults before main() -- and a static program cannot
 * dlopen.  So _citrus_load_module() failed for every module: setlocale(LC_CTYPE,
 * "en_US.UTF-8") silently stayed in the C locale (with nl_langinfo(CODESET)
 * still reporting "646"), and every iconv_open() failed with EINVAL.  Note that
 * libc is compiled with -D_I18N_DYNAMIC only for the *shared* library, so the
 * static libc did not even have the dlopen path: it got the stub that always
 * returns EINVAL.
 *
 * The data was never the problem -- /usr/share/i18n holds the esdb and csmapper
 * tables, and esdb.alias correctly maps 646 to ISO646-US.  Only the loader was.
 *
 * So compile the modules that matter into libc and consult a table before
 * reaching for dlopen.  What is needed and why:
 *
 *   UTF8            multibyte encodings need a ctype/stdenc module; single-byte
 *                   ones already use "NONE", which has always been part of libc
 *                   (which is why the C and ISO8859 locales worked and UTF-8 did
 *                   not).
 *   iconv_std       the table-driven iconv engine -- every real conversion.
 *   iconv_none      the identity converter.
 *   mapper_*        the mappers csmapper drives to build a conversion.
 *
 * The legacy CJK encodings (BIG5, EUC, ISO2022, ...) are deliberately left out.
 * They are a lot of code for something nothing in the base system asks for, and
 * a dynamically linked program can still dlopen them as before.
 */

#include <sys/cdefs.h>

#include <sys/types.h>
#include <sys/queue.h>		/* citrus_iconv_local.h uses TAILQ_ENTRY */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/*
 * The same include chain the module sources themselves use, and in the same
 * order: the citrus *_local.h headers that the module headers pull in take their
 * types from these and do not include them themselves.
 */
#include "citrus_namespace.h"
#include "citrus_types.h"
#include "citrus_bcs.h"
#include "citrus_module.h"
#include "citrus_region.h"
#include "citrus_memstream.h"
#include "citrus_mmap.h"
#include "citrus_hash.h"
#include "citrus_ctype.h"
#include "citrus_stdenc.h"
#include "citrus_iconv.h"
#include "citrus_mapper.h"
#include "citrus_csmapper.h"
#include "citrus_esdb.h"

#include "citrus_utf8.h"
#include "citrus_iconv_std.h"
#include "citrus_iconv_none.h"
#include "citrus_mapper_std.h"
#include "citrus_mapper_none.h"
#include "citrus_mapper_646.h"
#include "citrus_mapper_serial.h"
#include "citrus_mapper_zone.h"

#include "citrus_module_builtin.h"

/*
 * One row per (module, interface) pair, mirroring the symbol dlsym() would have
 * looked up: _citrus_<module>_<interface>_getops.  An encoding module answers to
 * both "ctype" and "stdenc"; iconv engines to "iconv"; mappers to "mapper".
 */
struct _citrus_builtin_module {
	const char	*bm_module;
	const char	*bm_interface;
	void		*bm_getops;
};

/*
 * Casting a function pointer to void * is not strictly conforming C, but it is
 * exactly what dlsym() does, and _citrus_find_getops()'s callers cast it straight
 * back to the right prototype.
 */
#define	BUILTIN(m, i)	{ #m, #i, (void *)(uintptr_t)_citrus_##m##_##i##_getops }

static const struct _citrus_builtin_module _citrus_builtin_modules[] = {
	/* Encodings.  Single-byte ones use the built-in NONE and need no entry. */
	BUILTIN(UTF8, ctype),
	BUILTIN(UTF8, stdenc),

	/* iconv engines. */
	BUILTIN(iconv_std, iconv),
	BUILTIN(iconv_none, iconv),

	/* Mappers, as driven by csmapper. */
	BUILTIN(mapper_std, mapper),
	BUILTIN(mapper_none, mapper),
	BUILTIN(mapper_646, mapper),
	BUILTIN(mapper_serial, mapper),
	BUILTIN(mapper_zone, mapper),
};

/*
 * The handle we hand back for a built-in module is simply a pointer to its first
 * row.  That makes it non-NULL (which is all citrus asks of a handle) and lets
 * _citrus_is_builtin_module() recognise it later by a range check, so we never
 * try to dlclose() something that was never dlopen'd.
 */
_citrus_module_t
_citrus_find_builtin_module(const char *modname)
{
	size_t i;

	_DIAGASSERT(modname != NULL);

	for (i = 0; i < __arraycount(_citrus_builtin_modules); i++)
		if (strcmp(_citrus_builtin_modules[i].bm_module, modname) == 0)
			return (_citrus_module_t)
			    __UNCONST(&_citrus_builtin_modules[i]);

	return NULL;
}

int
_citrus_is_builtin_module(_citrus_module_t handle)
{
	const struct _citrus_builtin_module *bm = (const void *)handle;

	return (bm >= &_citrus_builtin_modules[0] &&
	    bm < &_citrus_builtin_modules[__arraycount(_citrus_builtin_modules)]);
}

void *
_citrus_find_builtin_getops(const char *modname, const char *ifname)
{
	size_t i;

	_DIAGASSERT(modname != NULL);
	_DIAGASSERT(ifname != NULL);

	for (i = 0; i < __arraycount(_citrus_builtin_modules); i++) {
		const struct _citrus_builtin_module *bm =
		    &_citrus_builtin_modules[i];

		if (strcmp(bm->bm_module, modname) == 0 &&
		    strcmp(bm->bm_interface, ifname) == 0)
			return bm->bm_getops;
	}

	return NULL;
}
