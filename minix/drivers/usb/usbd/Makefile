# Makefile for usb host controllers
.include <bsd.own.mk>

.if ${MACHINE_ARCH} == "earm"
SUBDIR= .WAIT base/earm
.endif

.if ${MACHINE_ARCH} == "x86_64"
SUBDIR= .WAIT base/amd64
.endif

.include <bsd.subdir.mk>
