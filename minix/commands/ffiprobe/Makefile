PROG=	ffiprobe
MAN=

# Link libffi straight from its build-tree objdir (PROGDPLIBS emits
# -L<objdir> -lffi and the DPADD), not "-lffi" against ${DESTDIR}/usr/lib.
# libffi is built earlier in the same world build but is not guaranteed to be
# installed into DESTDIR by the time this probe links during a clean or an
# interrupted "-u" dependall -- see the note at bsd.prog.mk's PROGDPLIBS block.
PROGDPLIBS+=	ffi	${NETBSDSRCDIR}/external/mit/libffi/lib/libffi

.include <bsd.prog.mk>
