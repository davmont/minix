#	LLVM 22 moved each tool's main() into a generated <prog>-driver.cpp that
#	calls <LLVM_DRIVER_TOOL>_main(); generate it from the shared template.
#	Default tool name is the program name with '-' replaced by '_'; tools whose
#	entry point differs (e.g. llvm-ar -> ar_main) override LLVM_DRIVER_TOOL.

LLVM_DRIVER_TOOL?=	${PROG_CXX:S/-/_/g}

SRCS+=		${PROG_CXX}-driver.cpp
CLEANFILES+=	${PROG_CXX}-driver.cpp

${PROG_CXX}-driver.cpp: ${LLVM_SRCDIR}/cmake/modules/llvm-driver-template.cpp.in
	${TOOL_SED} -e 's/@TOOL_NAME@/${LLVM_DRIVER_TOOL}/g' < ${.ALLSRC} > ${.TARGET}
