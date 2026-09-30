#	$NetBSD$
#
# MINIX: libc++ 22 has ~1700 headers across ~90 __*/ subdirs, far too many to
# hand-list, so generate the install set from the source tree.  Our own
# __config_site + __assertion_handler (in .CURDIR) and libcxxrt's cxxabi.h are
# added explicitly.

.include <bsd.init.mk>

.PATH: ${LIBCXX_SRCDIR}/include

INCSDIR=	/usr/include/c++

INCS!=	cd ${LIBCXX_SRCDIR}/include && \
	find . -type f ! -name '*.in' ! -name 'CMakeLists.txt' | \
	sed -e 's|^\./||' | sort

# MINIX-provided / ABI headers (from .CURDIR and libcxxrt).
INCS+=	__config_site __assertion_handler cxxabi.h

# Parallel make can reach this rule from two places at once (INCS and
# DPSRCS); a plain cp then fails with "File exists".  Copy to a private
# name and rename, which is atomic.
cxxabi.h: ${LIBCXXRT_SRCDIR}/src/cxxabi.h
	cp ${LIBCXXRT_SRCDIR}/src/cxxabi.h ${.TARGET}.tmp.$$$$ && \
	    mv -f ${.TARGET}.tmp.$$$$ ${.TARGET}

DPSRCS+=	cxxabi.h
CLEANFILES+=	cxxabi.h

.include <bsd.clean.mk>
.include <bsd.inc.mk>
.include <bsd.obj.mk>
