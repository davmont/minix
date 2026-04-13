# Makefile for arch-dependent TTY code (x86-64)
# x86-64 uses the same PC TTY hardware as i386.
.include <bsd.own.mk>

HERE=${.CURDIR}/arch/i386
.PATH:  ${HERE}

SRCS +=	console.c keyboard.c rs232.c
