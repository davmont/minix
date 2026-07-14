/*	$MINIX$	*/

/*
 * Lookup of the citrus modules compiled into libc.  See citrus_module_builtin.c
 * for why they exist: MINIX links statically, and a static program cannot dlopen
 * the modules in /usr/lib/i18n.
 */

#ifndef _CITRUS_MODULE_BUILTIN_H_
#define _CITRUS_MODULE_BUILTIN_H_

__BEGIN_DECLS
/* Returns a handle for a module compiled into libc, or NULL if it is not one. */
_citrus_module_t _citrus_find_builtin_module(const char *);

/* True if the handle came from _citrus_find_builtin_module(), i.e. must not be
 * dlclose()d. */
int _citrus_is_builtin_module(_citrus_module_t);

/* The getops entry point dlsym() would have returned, or NULL. */
void *_citrus_find_builtin_getops(const char *, const char *);
__END_DECLS

#endif /* _CITRUS_MODULE_BUILTIN_H_ */
