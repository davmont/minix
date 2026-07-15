#	MINIX 3: build llvm-objcopy as a host tool, to replace binutils' objcopy.

HOSTPROGNAME=	${MACHINE_GNU_PLATFORM}-objcopy
HOSTPROG_CXX=	${MACHINE_GNU_PLATFORM}-objcopy
HOST_SRCDIR=	external/apache2/llvm/bin/llvm-objcopy

.include "${.CURDIR}/../Makefile.host"

# llvm-objcopy behaves as strip when invoked as "strip" (it switches on argv[0]),
# which is how upstream ships llvm-strip.
install.host: ${HOST_BINDIR}/${MACHINE_GNU_PLATFORM}-strip

${HOST_BINDIR}/${MACHINE_GNU_PLATFORM}-strip:
	${_MKTARGET_INSTALL}
	mkdir -p ${HOST_BINDIR}
	${HOST_INSTALL_SYMLINK} ${HOSTPROGNAME} ${.TARGET}
