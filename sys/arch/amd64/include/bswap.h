/*	$NetBSD: bswap.h,v 1.4 2008/10/26 06:57:30 mrg Exp $	*/

/* Written by Manuel Bouyer. Public domain */

#ifndef _AMD64_BSWAP_H_
#define	_AMD64_BSWAP_H_

#include <machine/byte_swap.h>

/*
 * When building host tools (compat/nbtool build), compat_defs.h already
 * provides bswap16/32/64 macros. Including sys/bswap.h would cause conflicts
 * because it tries to declare bswap16(uint16_t) after bswap16 is a macro.
 */
#if !defined(HAVE_NBTOOL_CONFIG_H)
#define __BSWAP_RENAME
#include <sys/bswap.h>
#endif

#endif /* !_AMD64_BSWAP_H_ */
