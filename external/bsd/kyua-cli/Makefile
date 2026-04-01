# $NetBSD: Makefile,v 1.2 2013/02/24 03:06:15 jmmv Exp $

.if !defined(__MINIX)
SUBDIR= etc lib .WAIT share tests usr.bin

.for dir in share tests usr.bin
includes-${dir}: .PHONY
	@true
.endfor
.else
# Tests disabled on MINIX: libatf-c++ in cross-compilation sysroot is incomplete
SUBDIR= etc lib .WAIT share usr.bin

.for dir in share usr.bin
includes-${dir}: .PHONY
	@true
.endfor
.endif

.include <bsd.subdir.mk>
