# Makefile for arch-dependent readclock code (x86-64)
# x86-64 uses the same CMOS clock hardware as i386.
.include <bsd.own.mk>

HERE=${.CURDIR}/arch/i386
.PATH:  ${HERE}

SRCS += arch_readclock.c
