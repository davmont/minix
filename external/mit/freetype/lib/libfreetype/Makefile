#	MINIX: freetype - single-object-per-module build (see dist/INSTALL.ANY).

.include <bsd.init.mk>

LIB=		freetype

FTSRC=		${FREETYPESRCDIR}/src

# Base layer.  ftbase.c aggregates most base sources via #include; the
# files listed here are the standalone base translation units.
.PATH:	${FTSRC}/base
SRCS=	ftsystem.c ftinit.c ftdebug.c ftbase.c \
	ftbbox.c ftbdf.c ftbitmap.c ftcid.c ftfstype.c ftgasp.c \
	ftglyph.c ftgxval.c ftmm.c ftotval.c ftpatent.c ftpfr.c \
	ftstroke.c ftsynth.c fttype1.c ftwinfnt.c

# Font drivers and helper modules - each is a single aggregating TU that
# #includes its module's real sources.
.PATH:	${FTSRC}/autofit
.PATH:	${FTSRC}/bdf
.PATH:	${FTSRC}/cff
.PATH:	${FTSRC}/cid
.PATH:	${FTSRC}/gxvalid
.PATH:	${FTSRC}/otvalid
.PATH:	${FTSRC}/pcf
.PATH:	${FTSRC}/pfr
.PATH:	${FTSRC}/psaux
.PATH:	${FTSRC}/pshinter
.PATH:	${FTSRC}/psnames
.PATH:	${FTSRC}/raster
.PATH:	${FTSRC}/sdf
.PATH:	${FTSRC}/sfnt
.PATH:	${FTSRC}/smooth
.PATH:	${FTSRC}/truetype
.PATH:	${FTSRC}/type1
.PATH:	${FTSRC}/type42
.PATH:	${FTSRC}/winfonts
.PATH:	${FTSRC}/gzip
.PATH:	${FTSRC}/lzw
.PATH:	${FTSRC}/cache
.PATH:	${FTSRC}/svg

SRCS+=	autofit.c bdf.c cff.c type1cid.c gxvalid.c otvalid.c \
	pcf.c pfr.c psaux.c pshinter.c psnames.c raster.c sdf.c \
	sfnt.c smooth.c truetype.c type1.c type42.c winfnt.c \
	ftgzip.c ftlzw.c ftcache.c svg.c


# Install the FreeType 2 public header tree under /usr/include/freetype2.
# ft2build.h sits at the top; the rest live in freetype/ and freetype/config/.
.PATH:	${FREETYPESRCDIR}/include
INCS=		ft2build.h
INCSDIR=	/usr/include/freetype2

.PATH:	${FREETYPESRCDIR}/include/freetype
FT_HDRS!=	cd ${FREETYPESRCDIR}/include/freetype && echo *.h
.for _h in ${FT_HDRS}
INCS+=		${_h}
INCSDIR_${_h}=	/usr/include/freetype2/freetype
.endfor

.PATH:	${FREETYPESRCDIR}/include/freetype/config
FT_CFG!=	cd ${FREETYPESRCDIR}/include/freetype/config && echo *.h
.for _h in ${FT_CFG}
INCS+=		${_h}
INCSDIR_${_h}=	/usr/include/freetype2/freetype/config
.endfor

CPPFLAGS+=	-I${FREETYPESRCDIR}/include
CPPFLAGS+=	-DFT2_BUILD_LIBRARY

# FreeType is not -Werror clean under an arbitrary compiler flag set.
COPTS+=		-Wno-error
COPTS+=		-Wno-unknown-warning-option

FILES+=			freetype2.pc
FILESDIR_freetype2.pc=	/usr/lib/pkgconfig
CLEANFILES+=		freetype2.pc

.include <bsd.lib.mk>

freetype2.pc: freetype2.pc.in
	@${TOOL_SED} -e 's/@VERSION@/2.14.3/' < ${.CURDIR}/freetype2.pc.in > ${.TARGET}

all: freetype2.pc
dependall: all
