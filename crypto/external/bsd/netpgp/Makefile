#	$NetBSD: Makefile,v 1.10 2018/04/29 19:47:34 sevan Exp $

SUBDIR+=	libmj .WAIT
SUBDIR+=	lib .WAIT
.if !defined(__MINIX)
# The Lua bindings need a Lua interpreter that MINIX doesn't ship.
SUBDIR+= 	bindings .WAIT
.endif
SUBDIR+=	bin

.include <bsd.subdir.mk>
