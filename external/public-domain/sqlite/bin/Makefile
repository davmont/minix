# $NetBSD: Makefile,v 1.8 2020/11/12 17:53:43 nia Exp $

PROG=		sqlite3

SRCS=		shell.c

# sqlite 3.53's shell.c indexes ctype-style tables with a plain (possibly
# signed) char (isspace(c)/IS_B85(c)); silence the resulting -Wchar-subscripts
# so it does not trip -Werror.
COPTS.shell.c+=	-Wno-error=char-subscripts

DPADD+=		${LIBSQLITE3} ${LIBEDIT} ${LIBTERIMINFO} ${LIBM}
LDADD+=		-lsqlite3 -ledit -lterminfo -lm

BINDIR=		/usr/bin

.include <bsd.prog.mk>
