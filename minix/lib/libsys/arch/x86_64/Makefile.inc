# Makefile for arch-dependent libsys code (amd64)
#
# All of these files use the kernel-call mechanism exclusively (no
# inline assembly), so they are identical in behaviour on amd64 and
# i386.  We point .PATH at the i386 arch directory to reuse them
# rather than duplicating the sources.
.include <bsd.own.mk>

HERE=${.CURDIR}/arch/i386
.PATH:  ${HERE}

SRCS+=  \
        get_randomness.c \
        getidle.c \
        ser_putc.c \
        spin.c \
        sys_eniop.c \
        sys_in.c \
        sys_out.c \
        sys_readbios.c \
        sys_sdevio.c \
        sys_umap_remote.c \
        sys_vinb.c \
        sys_vinl.c \
        sys_vinw.c \
        sys_voutb.c \
        sys_voutl.c \
        sys_voutw.c \
        tsc_util.c \
        vbox.c

CPPFLAGS+=        -I${HERE}/../../
