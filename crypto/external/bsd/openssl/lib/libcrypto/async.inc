#	$NetBSD: async.inc,v 1.1.14.1 2023/08/11 13:41:54 martin Exp $

.PATH:	${OPENSSLSRC}/crypto/async ${OPENSSLSRC}/crypto/async/arch

ASYNC_SRCS += \
async.c \
async_err.c \
async_wait.c \

# Pick the right arch source.  MINIX now defines OPENSSL_THREADS (libpthread
# exists), so async_posix.c is a real TU and is used like every other platform.
ASYNC_SRCS += async_posix.c

SRCS += ${ASYNC_SRCS}

.for cryptosrc in ${ASYNC_SRCS}
CPPFLAGS.${cryptosrc} += -I${OPENSSLSRC}/crypto/async ${ASYNCCPPFLAGS}
.endfor
