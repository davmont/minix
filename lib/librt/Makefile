# POSIX asynchronous I/O (aio_read(), lio_listio() and co.), which MINIX does
# with threads rather than in the kernel: see aio.c.

WARNS?=	5

LIB=	rt
SRCS=	aio.c

CPPFLAGS+=	-D_REENTRANT
LIBDPLIBS+=	pthread	${.CURDIR}/../libpthread

.include <bsd.lib.mk>
