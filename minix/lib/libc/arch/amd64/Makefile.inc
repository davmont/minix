C_HERE=${NETBSDSRCDIR}/minix/lib/libc/arch/${ARCHSUBDIR}
.PATH: ${C_HERE}

SRCS+=	_cpuid.S	\
	get_bp.S	\
	_cpufeature.c
