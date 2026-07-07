# $NetBSD$

.include <bsd.init.mk>

LIB=		uv

.PATH:	${LIBUVSRCDIR}/src ${LIBUVSRCDIR}/src/unix

# Platform-independent core.
SRCS+=	fs-poll.c idna.c inet.c random.c strscpy.c strtok.c thread-common.c \
	threadpool.c timer.c uv-common.c uv-data-getter-setters.c version.c

# Generic Unix backend (shared by all unix ports).
SRCS+=	async.c core.c dl.c fs.c getaddrinfo.c getnameinfo.c loop-watcher.c \
	loop.c pipe.c poll.c process.c random-devurandom.c signal.c stream.c \
	tcp.c thread.c tty.c udp.c

# MINIX uses the portable poll(2) event loop (no kqueue/epoll), plus the
# generic-POSIX helpers, mirroring the GNU/Hurd profile.
SRCS+=	bsd-ifaddrs.c no-fsevents.c no-proctitle.c posix-hrtime.c \
	posix-poll.c minix.c

CPPFLAGS+=	-I${LIBUVSRCDIR}/include -I${LIBUVSRCDIR}/src

# libuv's uv__fs_scandir passes a dirent**-typed comparator to scandir(3), whose
# MINIX/NetBSD prototype takes int(*)(const void*, const void*).  clang >= 16
# promotes this mismatch to a hard error by default; it is ABI-compatible, so
# demote it to a warning for this vendored upstream source.
CFLAGS+=	-Wno-error=incompatible-function-pointer-types

# libuv is thread-based.
LIBDPLIBS+=	pthread	${NETBSDSRCDIR}/lib/libpthread

# Installed headers: uv.h + the uv/ subdirectory.
INCS=		uv.h
# uv/stdint-msvc2008.h was removed upstream in libuv 1.45.
INCS+=		uv/aix.h uv/bsd.h uv/darwin.h uv/errno.h uv/linux.h uv/os390.h \
		uv/posix.h uv/sunos.h uv/threadpool.h \
		uv/tree.h uv/unix.h uv/version.h uv/win.h
INCSDIR=	/usr/include
.PATH:	${LIBUVSRCDIR}/include

# Vendored third-party source: keep warnings but don't fail the build on them
# (e.g. MINIX's old-style scandir(3) comparator prototype).  Genuine errors
# still fail.
WARNS?=		1
COPTS+=		-Wno-error

.include <bsd.lib.mk>
