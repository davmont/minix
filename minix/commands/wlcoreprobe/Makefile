PROG=	wlcoreprobe
MAN=

.include <bsd.own.mk>

# Link libwayland-server and libffi from their build-tree objdirs (the -L
# below), not ${DESTDIR}/usr/lib, so this probe links even before they are
# installed during a clean or interrupted "-u" world build.  libwayland-server
# dispatches protocol messages through ffi_call() and uses pthread_once/
# pthread_key, so it must precede libffi/pthread/m in the -l order.
WLSERVEROBJDIR!=	cd ${NETBSDSRCDIR}/external/mit/wayland/lib/libwayland-server && ${PRINTOBJDIR}
FFIOBJDIR!=		cd ${NETBSDSRCDIR}/external/mit/libffi/lib/libffi && ${PRINTOBJDIR}
LDADD+=	-L${WLSERVEROBJDIR} -L${FFIOBJDIR} \
	-lwayland-server -lffi -lpthread -lm
DPADD+=	${WLSERVEROBJDIR}/libwayland-server.a ${FFIOBJDIR}/libffi.a

.include <bsd.prog.mk>
