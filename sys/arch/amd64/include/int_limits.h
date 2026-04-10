/*	$NetBSD: int_limits.h,v 1.9 2014/07/25 21:43:13 joerg Exp $	*/

/*-
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _AMD64_INT_LIMITS_H_
#define _AMD64_INT_LIMITS_H_

#ifdef __SIG_ATOMIC_MAX__
#include <sys/common_int_limits.h>
#else

/*
 * 7.18.2 Limits of specified-width integer types (LP64)
 */

/* 7.18.2.1 Limits of exact-width integer types */
#define	INT8_MIN	(-0x7f-1)
#define	INT16_MIN	(-0x7fff-1)
#define	INT32_MIN	(-0x7fffffff-1)
#define	INT64_MIN	(-0x7fffffffffffffffL-1)

#define	INT8_MAX	0x7f
#define	INT16_MAX	0x7fff
#define	INT32_MAX	0x7fffffff
#define	INT64_MAX	0x7fffffffffffffffL

#define	UINT8_MAX	0xff
#define	UINT16_MAX	0xffff
#define	UINT32_MAX	0xffffffffU
#define	UINT64_MAX	0xffffffffffffffffUL

/* 7.18.2.2 Limits of minimum-width integer types */
#define	INT_LEAST8_MIN	(-0x7f-1)
#define	INT_LEAST16_MIN	(-0x7fff-1)
#define	INT_LEAST32_MIN	(-0x7fffffff-1)
#define	INT_LEAST64_MIN	(-0x7fffffffffffffffL-1)

#define	INT_LEAST8_MAX	0x7f
#define	INT_LEAST16_MAX	0x7fff
#define	INT_LEAST32_MAX	0x7fffffff
#define	INT_LEAST64_MAX	0x7fffffffffffffffL

#define	UINT_LEAST8_MAX	 0xff
#define	UINT_LEAST16_MAX 0xffff
#define	UINT_LEAST32_MAX 0xffffffffU
#define	UINT_LEAST64_MAX 0xffffffffffffffffUL

/* 7.18.2.3 Limits of fastest minimum-width integer types */
#define	INT_FAST8_MIN	(-0x7f-1)
#define	INT_FAST16_MIN	(-0x7fffffff-1)
#define	INT_FAST32_MIN	(-0x7fffffff-1)
#define	INT_FAST64_MIN	(-0x7fffffffffffffffL-1)

#define	INT_FAST8_MAX	0x7f
#define	INT_FAST16_MAX	0x7fffffff
#define	INT_FAST32_MAX	0x7fffffff
#define	INT_FAST64_MAX	0x7fffffffffffffffL

#define	UINT_FAST8_MAX	0xff
#define	UINT_FAST16_MAX	0xffffffffU
#define	UINT_FAST32_MAX	0xffffffffU
#define	UINT_FAST64_MAX	0xffffffffffffffffUL

/* 7.18.2.4 Limits of integer types capable of holding object pointers (LP64) */
#define	INTPTR_MIN	(-0x7fffffffffffffffL-1)
#define	INTPTR_MAX	0x7fffffffffffffffL
#define	UINTPTR_MAX	0xffffffffffffffffUL

/* 7.18.2.5 Limits of greatest-width integer types */
#define	INTMAX_MIN	(-0x7fffffffffffffffL-1)
#define	INTMAX_MAX	0x7fffffffffffffffL
#define	UINTMAX_MAX	0xffffffffffffffffUL

/* limits of ptrdiff_t (LP64) */
#define	PTRDIFF_MIN	(-0x7fffffffffffffffL-1)
#define	PTRDIFF_MAX	0x7fffffffffffffffL

/* limits of sig_atomic_t */
#define	SIG_ATOMIC_MIN	(-0x7fffffff-1)
#define	SIG_ATOMIC_MAX	0x7fffffff

/* limit of size_t (LP64) */
#define	SIZE_MAX	0xffffffffffffffffUL

#endif

#endif /* !_AMD64_INT_LIMITS_H_ */
