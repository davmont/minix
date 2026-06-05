#	$NetBSD: async.inc,v 1.1.14.1 2023/08/11 13:41:54 martin Exp $

.PATH:	${OPENSSLSRC}/crypto/async ${OPENSSLSRC}/crypto/async/arch

ASYNC_SRCS += \
async.c \
async_err.c \
async_wait.c \

# Pick the right arch source.  On MINIX (OPENSSL_THREADS is undefined
# because there's no libpthread), async_posix.c becomes an empty TU
# and we need async_null.c to provide the ASYNC_is_capable /
# async_local_cleanup stubs.
.if defined(__MINIX)
ASYNC_SRCS += async_null.c
.else
ASYNC_SRCS += async_posix.c
.endif

SRCS += ${ASYNC_SRCS}

.for cryptosrc in ${ASYNC_SRCS}
CPPFLAGS.${cryptosrc} += -I${OPENSSLSRC}/crypto/async ${ASYNCCPPFLAGS}
.endfor
