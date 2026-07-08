#	MINIX: pixman - generic C path (no SIMD fast paths built).

.include <bsd.init.mk>

LIB=		pixman-1

.PATH:	${PIXMANSRCDIR}/pixman

# Generic, portable core plus the per-arch dispatch stubs (which are
# no-ops without their USE_* macros).  SIMD implementations
# (pixman-{mmx,sse2,ssse3,arm-*,mips-dspr2,vmx,rvv,loongson-mmi}.c) and
# the region/combine template .c files are deliberately excluded.
SRCS=	pixman.c pixman-access.c pixman-access-accessors.c \
	pixman-arm.c pixman-bits-image.c \
	pixman-combine32.c pixman-combine-float.c \
	pixman-conical-gradient.c pixman-edge.c pixman-edge-accessors.c \
	pixman-fast-path.c pixman-filter.c pixman-general.c \
	pixman-glyph.c pixman-gradient-walker.c pixman-image.c \
	pixman-implementation.c pixman-linear-gradient.c pixman-matrix.c \
	pixman-mips.c pixman-noop.c pixman-ppc.c pixman-radial-gradient.c \
	pixman-region16.c pixman-region32.c pixman-region64f.c \
	pixman-riscv.c pixman-solid-fill.c pixman-trap.c pixman-utils.c \
	pixman-x86.c

INCS=		pixman.h pixman-version.h
INCSDIR=	/usr/include/pixman-1

# Public headers come from dist; pixman-version.h and pixman-config.h are
# curated here (dist has only the .in template / meson-generated config).
CPPFLAGS+=	-I${.CURDIR}/include -I${PIXMANSRCDIR}/pixman
CPPFLAGS+=	-DHAVE_CONFIG_H
# pixman needs the public pixman.h (dist) plus our pixman-version.h (here).
.PATH:	${.CURDIR}/include

# pixman is warning-heavy; it builds -Werror-clean upstream only with its
# own flag set, so relax fatal warnings for the vendored TUs.
COPTS+=		-Wno-error
COPTS+=		-Wno-unknown-warning-option

FILES+=			pixman-1.pc
FILESDIR_pixman-1.pc=	/usr/lib/pkgconfig
CLEANFILES+=		pixman-1.pc

.include <bsd.lib.mk>

pixman-1.pc: pixman-1.pc.in
	@${TOOL_SED} -e 's/@VERSION@/0.46.4/' < ${.CURDIR}/pixman-1.pc.in > ${.TARGET}

all: pixman-1.pc
dependall: all
