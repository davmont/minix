# Makefile for memory driver (MEMORY)
.include <bsd.own.mk>

USE_BITCODE:=no

PROG=	memory
SRCS=	memory.c imgrd.mfs
OBJS=	${SRCS:N*.h:R:S/$/.o/g}
MKBUILDEXT2RD?=	no

RAMDISK_PATH= ${NETBSDSRCDIR}/minix/drivers/storage/ramdisk
DPADD+=	${LIBBLOCKDRIVER} ${LIBCHARDRIVER}
LDADD+=	-lblockdriver -lchardriver

CPPFLAGS.memory.c+=	-I${NETBSDSRCDIR}/minix

imgrd.d: touch-genfiles
touch-genfiles:
	[ -e ../ramdisk/image ] || touch -t 197001020000.00 ../ramdisk/image


.SUFFIXES:      .mfs .c .o

# Map MACHINE_CPU to the BFD architecture name for objcopy's -B flag, and to the
# output-target name for -O.
#
# -O used to be given ${MACHINE_GNU_PLATFORM} -- "x86_64-elf64-minix".  That is a
# BFD target name GNU objcopy derives from the toolchain triple, and only GNU
# objcopy knows it: llvm-objcopy carries a fixed table of target names and
# rejects anything else outright ("invalid output format").  Since this is the
# only place in the tree that names a target that way, and GNU objcopy is equally
# happy with the generic name -- the object it produces is byte-for-byte the same
# -- use the generic one, so either objcopy can build the boot ramdisk.
.if ${MACHINE_CPU} == "x86_64"
OBJCOPY_ARCH=	i386:x86-64
OBJCOPY_TARGET=	elf64-x86-64
.elif ${MACHINE_CPU} == "i386"
OBJCOPY_ARCH=	i386
OBJCOPY_TARGET=	elf32-i386
.else
OBJCOPY_ARCH=	${MACHINE_CPU}
OBJCOPY_TARGET=	${MACHINE_GNU_PLATFORM}
.endif

.mfs.o:
	${_MKTARGET_CREATE}
	${OBJCOPY} -Ibinary -B${OBJCOPY_ARCH} -O${OBJCOPY_TARGET} $< $@

CLEANFILES+=	../ramdisk/image
# BJG - don't invoke parallel Makes
#../ramdisk/image: .PHONY
#	${MAKE} -C ${RAMDISK_PATH} image

CLEANFILES+=	imgrd.mfs
imgrd.mfs: ../ramdisk/image
	${HOST_LN} -fs ../ramdisk/image $@

.include <minix.service.mk>
