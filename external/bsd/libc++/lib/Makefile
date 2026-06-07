#	$NetBSD: Makefile,v 1.14 2021/05/30 01:57:00 joerg Exp $

LIB=		c++
WARNS=		4
NOLINT=		# defined

.include <bsd.init.mk>

.PATH: ${LIBCXX_SRCDIR}/src

# libc++ 22 src set (typeinfo.cpp omitted: libcxxrt provides type_info).
SRCS=	algorithm.cpp any.cpp atomic.cpp barrier.cpp bind.cpp call_once.cpp \
	charconv.cpp chrono.cpp condition_variable.cpp \
	condition_variable_destructor.cpp error_category.cpp exception.cpp \
	expected.cpp fstream.cpp functional.cpp future.cpp hash.cpp ios.cpp \
	ios.instantiations.cpp iostream.cpp locale.cpp memory.cpp \
	memory_resource.cpp mutex.cpp mutex_destructor.cpp new.cpp \
	new_handler.cpp new_helpers.cpp optional.cpp ostream.cpp print.cpp \
	random.cpp random_shuffle.cpp regex.cpp shared_mutex.cpp stdexcept.cpp \
	string.cpp strstream.cpp system_error.cpp thread.cpp valarray.cpp \
	variant.cpp vector.cpp verbose_abort.cpp

.PATH: ${LIBCXX_SRCDIR}/src/filesystem
SRCS+=	directory_entry.cpp directory_iterator.cpp filesystem_clock.cpp \
	filesystem_error.cpp int128_builtins.cpp operations.cpp path.cpp

.PATH: ${LIBCXX_SRCDIR}/src/ryu
SRCS+=	d2fixed.cpp d2s.cpp f2s.cpp

# typeinfo.cc: prefer libcxxrt's version

LIBCXXRT_SRCS+= \
	auxhelper.cc dynamic_cast.cc exception.cc guard.cc \
	stdexcept.cc terminate.cc typeinfo.cc libelftc_dem_gnu3.c
# cxa_atexit.c: Solaris-only
# memory.cc: already provided by libc++'s new.cpp

.for src in ${LIBCXXRT_SRCS}
rt_${src}: ${LIBCXXRT_SRCDIR}/src/${src}
	cp ${LIBCXXRT_SRCDIR}/src/${src} rt_${src}

SRCS+=		rt_${src}
DPSRCS+=	rt_${src}
CLEANFILES+=	rt_${src}
.endfor

.include <bsd.own.mk>

CPPFLAGS+=	-nostdinc++ -cxx-isystem ${LIBCXX_SRCDIR}/include
CPPFLAGS+=	-cxx-isystem ${.CURDIR}/../include -I${LIBCXXRT_SRCDIR}/src
CPPFLAGS+=	-DLIBCXXRT -D_LIBCPP_BUILDING_LIBRARY
# libc++ 22 from_chars shares float-parsing headers with llvm-libc
# (shared/fp_bits.h -> src/__support/...); add the imported libc include root.
CPPFLAGS+=	-I${LIBCXX_SRCDIR}/../libc
# ryu float<->string code includes "include/ryu/ryu.h" relative to src/.
CPPFLAGS+=	-I${LIBCXX_SRCDIR}/src
CXXFLAGS+=	-std=gnu++23

CWARNFLAGS.gcc+=	-Wno-error

CWARNFLAGS.clang+=	-Wno-error=unused-variable -Wno-error=missing-noreturn
CWARNFLAGS.clang+=	-Wno-error=missing-prototypes -Wno-error=sometimes-uninitialized
CWARNFLAGS.clang+=	-Wno-error=missing-field-initializers -Wno-error=switch
CWARNFLAGS.clang+=	-Wno-error=implicit-exception-spec-mismatch -Wno-error=format-nonliteral -Wno-error=format-security

.if ${MKSANITIZER} != "yes"
LDFLAGS+=	-Wl,-z,defs
.endif

.if !empty(MACHINE_ARCH:Marm*) || !empty(MACHINE_ARCH:Mearm*)
CWARNFLAGS+=		-Wno-atomic-alignment
.endif

.include <bsd.lib.mk>
