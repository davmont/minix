PROG=	wlcoreprobe
MAN=

# libwayland-server dispatches protocol messages through ffi_call(), and
# wayland-shm.c uses pthread_once/pthread_key.
LDADD+=	-lwayland-server -lffi -lpthread -lm
DPADD+=	${LIBWAYLAND_SERVER} ${LIBFFI} ${LIBPTHREAD} ${LIBM}

.include <bsd.prog.mk>
