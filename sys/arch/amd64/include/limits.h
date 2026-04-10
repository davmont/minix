/*	$NetBSD: limits.h (adapted for amd64/LP64)	*/

#ifndef _AMD64_LIMITS_H_
#define _AMD64_LIMITS_H_

#include <sys/featuretest.h>

#define	CHAR_BIT	8		/* number of bits in a char */
#define	UCHAR_MAX	0xff		/* max value for an unsigned char */
#define	SCHAR_MAX	0x7f		/* max value for a signed char */
#define SCHAR_MIN	(-0x7f-1)	/* min value for a signed char */
#define	USHRT_MAX	0xffff		/* max value for an unsigned short */
#define	SHRT_MAX	0x7fff		/* max value for a short */
#define SHRT_MIN	(-0x7fff-1)	/* min value for a short */
#define	UINT_MAX	0xffffffffU	/* max value for an unsigned int */
#define	INT_MAX		0x7fffffff	/* max value for an int */
#define	INT_MIN		(-0x7fffffff-1)	/* min value for an int */
/* LP64: long is 64 bits on amd64 */
#define	ULONG_MAX	0xffffffffffffffffUL	/* max value for an unsigned long */
#define	LONG_MAX	0x7fffffffffffffffL	/* max value for a long */
#define	LONG_MIN	(-0x7fffffffffffffffL-1)/* min value for a long */

#if defined(_POSIX_C_SOURCE) || defined(_XOPEN_SOURCE) || \
    defined(_NETBSD_SOURCE)
/* ssize_t is long on LP64 */
#define	SSIZE_MAX	LONG_MAX	/* max value for a ssize_t */
#if defined(_ISOC99_SOURCE) || (__STDC_VERSION__ - 0) >= 199901L || \
    defined(_NETBSD_SOURCE)
#define	ULLONG_MAX	0xffffffffffffffffULL	/* max unsigned long long */
#define	LLONG_MAX	0x7fffffffffffffffLL	/* max signed long long */
#define	LLONG_MIN	(-0x7fffffffffffffffLL-1) /* min signed long long */
#endif

#if defined(_NETBSD_SOURCE)
#define	SSIZE_MIN	LONG_MIN	/* min value for a ssize_t */
#define	SIZE_T_MAX	ULONG_MAX	/* max value for a size_t */

#define	UQUAD_MAX	0xffffffffffffffffULL		/* max unsigned quad */
#define	QUAD_MAX	0x7fffffffffffffffLL		/* max signed quad */
#define	QUAD_MIN	(-0x7fffffffffffffffLL-1)	/* min signed quad */
#endif /* _NETBSD_SOURCE */
#endif /* _POSIX_C_SOURCE || _XOPEN_SOURCE || _NETBSD_SOURCE */

#if defined(_XOPEN_SOURCE) || defined(_NETBSD_SOURCE)
#define LONG_BIT	64		/* LP64: 64-bit longs */
#define WORD_BIT	32

#define DBL_DIG		__DBL_DIG__
#define DBL_MAX		__DBL_MAX__
#define DBL_MIN		__DBL_MIN__

#define FLT_DIG		__FLT_DIG__
#define FLT_MAX		__FLT_MAX__
#define FLT_MIN		__FLT_MIN__
#endif

#endif /* _AMD64_LIMITS_H_ */
