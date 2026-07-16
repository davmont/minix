PROG=	xkbprobe
MAN=

.include <bsd.own.mk>

# Link libxkbcommon from its build-tree objdir (the -L below), not
# ${DESTDIR}/usr/lib, so this probe links even before libxkbcommon is installed
# during a clean or interrupted "-u" world build.
XKBOBJDIR!=	cd ${NETBSDSRCDIR}/external/mit/libxkbcommon/lib/libxkbcommon && ${PRINTOBJDIR}
LDADD+=	-L${XKBOBJDIR} -lxkbcommon -lm
DPADD+=	${XKBOBJDIR}/libxkbcommon.a

.include <bsd.prog.mk>
