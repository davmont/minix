PROG=	cxxprobe
SRCS=	cxxprobe.cc
MAN=

CXXFLAGS+=	-std=c++17

LDADD+=	-lc++ -lm
DPADD+=	${LIBCXX} ${LIBM}

.include <bsd.prog.mk>
