#	MINIX 3: build llvm-ar as a host tool, to replace binutils' ar.

HOSTPROGNAME=	${MACHINE_GNU_PLATFORM}-ar
HOSTPROG_CXX=	${MACHINE_GNU_PLATFORM}-ar
HOST_SRCDIR=	external/apache2/llvm/bin/llvm-ar

.include "${.CURDIR}/../Makefile.host"

# llvm-ar behaves as ranlib when invoked as "ranlib" (it switches on argv[0]),
# which is how upstream ships it.
install.host: ${HOST_BINDIR}/${MACHINE_GNU_PLATFORM}-ranlib

${HOST_BINDIR}/${MACHINE_GNU_PLATFORM}-ranlib:
	${_MKTARGET_INSTALL}
	mkdir -p ${HOST_BINDIR}
	${HOST_INSTALL_SYMLINK} ${HOSTPROGNAME} ${.TARGET}
