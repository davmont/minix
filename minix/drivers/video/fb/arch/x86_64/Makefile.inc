# Makefile for arch-dependent framebuffer code (x86_64 UEFI GOP)
.include <bsd.own.mk>

HERE=${.CURDIR}/arch/${MACHINE_ARCH}
.PATH:  ${HERE}

SRCS +=	fb_arch.c
