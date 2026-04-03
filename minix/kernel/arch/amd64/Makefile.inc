# Makefile for arch-dependent kernel code (amd64 / x86_64)
.include <bsd.own.mk>

HERE=${.CURDIR}/arch/${MACHINE_ARCH}
.PATH:	${HERE}

# objects we want unpaged from -lc
MINLIB_OBJS_UNPAGED= _cpufeature.o _cpuid.o get_bp.o
_cpufeature.o:	${NETBSDSRCDIR}/minix/lib/libc/arch/${MACHINE_ARCH}/_cpufeature.c
_cpuid.o:	${NETBSDSRCDIR}/minix/lib/libc/arch/${MACHINE_ARCH}/_cpuid.S
get_bp.o:	${NETBSDSRCDIR}/minix/lib/libc/arch/${MACHINE_ARCH}/get_bp.S

# objects we want unpaged from -lsys
SYS_OBJS_UNPAGED=assert.o stacktrace.o
assert.o:	${NETBSDSRCDIR}/minix/lib/libsys/assert.c
stacktrace.o:	${NETBSDSRCDIR}/minix/lib/libsys/stacktrace.c

# objects we want unpaged from -lminc
MINC_OBJS_UNPAGED= atoi.o \
	printf.o subr_prf.o \
	strcmp.o strcpy.o strlen.o strncmp.o \
	memcpy.o memmove.o memset.o \
	udivdi3.o umoddi3.o qdivrem.o
atoi.o:		${NETBSDSRCDIR}/minix/lib/libminc/atoi.c
printf.o:	${NETBSDSRCDIR}/sys/lib/libsa/printf.c
subr_prf.o:	${NETBSDSRCDIR}/sys/lib/libsa/subr_prf.c
memcpy.o:	${NETBSDSRCDIR}/common/lib/libc/arch/amd64/string/memcpy.S
memmove.o:	${NETBSDSRCDIR}/common/lib/libc/arch/amd64/string/memmove.S
memset.o:	${NETBSDSRCDIR}/common/lib/libc/arch/amd64/string/memset.S
strlen.o:	${NETBSDSRCDIR}/common/lib/libc/arch/amd64/string/strlen.S
strcpy.o:	${NETBSDSRCDIR}/common/lib/libc/arch/amd64/string/strcpy.S
strcmp.o:	${NETBSDSRCDIR}/common/lib/libc/arch/amd64/string/strcmp.S

# required by pre_init.c
strncmp.o:	${NETBSDSRCDIR}/common/lib/libc/string/strncmp.c

# required by kprintn.o
udivdi3.o:	${NETBSDSRCDIR}/common/lib/libc/quad/udivdi3.c
umoddi3.o:	${NETBSDSRCDIR}/common/lib/libc/quad/umoddi3.c
qdivrem.o:	${NETBSDSRCDIR}/common/lib/libc/quad/qdivrem.c

CPPFLAGS.subr_prf.c+= -Dputchar=kputc
CPPFLAGS.subr_prf.c+= -DLIBSA_PRINTF_LONGLONG_SUPPORT -DLIBSA_PRINTF_WIDTH_SUPPORT

# Compiler flags for 64-bit kernel
CFLAGS+=	-m64 -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2
CPPFLAGS+=	-D__amd64__ -D_LP64

#
# Objects that must be built in the unpaged (pre-paging) namespace.
# The build system prefixes their symbols with __k_unpaged_ so the linker
# can place them in the low-mapped .unpaged section.
#
.for unpaged_obj in head.o pre_init.o direct_tty_utils.o \
	pg_utils.o klib.o utility.o arch_reset.o \
	io_inb.o io_outb.o \
	${MINLIB_OBJS_UNPAGED} ${MINC_OBJS_UNPAGED} ${SYS_OBJS_UNPAGED}

CLEANFILES+=${unpaged_obj}.bin
.if ${USE_BITCODE:Uno} == "yes"
unpaged_${unpaged_obj}: ${unpaged_obj}
	if file -b ${.OBJDIR}/${unpaged_obj} | grep -q '^LLVM'; then \
		${LLC} -O1 -march=x86-64 -filetype=obj \
		    -o ${.OBJDIR}/${unpaged_obj}.bin ${.OBJDIR}/${unpaged_obj}; \
	else \
		cp ${.OBJDIR}/${unpaged_obj} ${.OBJDIR}/${unpaged_obj}.bin; \
	fi
	${OBJCOPY} --prefix-symbols=__k_unpaged_ ${.OBJDIR}/${unpaged_obj}.bin $@
.else
unpaged_${unpaged_obj}: ${unpaged_obj}
	${OBJCOPY} --prefix-symbols=__k_unpaged_ ${.OBJDIR}/${unpaged_obj} $@
.endif
UNPAGED_OBJS += unpaged_${unpaged_obj}
ORIG_UNPAGED_OBJS += ${unpaged_obj}
.endfor

CLEANFILES+= ${ORIG_UNPAGED_OBJS}

# Core paged kernel sources
SRCS+=	mpx.S head.S klib.S protect.c pg_utils.c pre_init.c \
	arch_clock.c arch_do_vmctl.c arch_system.c arch_reset.c \
	direct_tty_utils.c memory.c

SRCS+=	exception.c i8259.c \
	do_iopenable.c do_readbios.c do_sdevio.c \
	usermapped_data_arch.c \
	io_inb.S io_inl.S io_intr.S io_inw.S io_outb.S io_outl.S io_outw.S

OBJS.kernel+=	${UNPAGED_OBJS}

.if ${USE_ACPI} != "no"
SRCS+= acpi.c
CPPFLAGS+= -DUSE_ACPI
.endif

.if ${USE_APIC} != "no"
SRCS+= apic.c apic_asm.S
CPPFLAGS+= -DUSE_APIC
.endif

.if ${USE_DEBUGREG} != "no"
SRCS+=  breakpoints.c debugreg.S
.endif

.if ${USE_WATCHDOG} != "no"
SRCS+= arch_watchdog.c
CPPFLAGS+= -DUSE_WATCHDOG
.endif

head.o mpx.o klib.o: procoffsets.h sconst.h

SRCS+= procoffsets.h

PROCOFFSETSCF=procoffsets.cf

.PATH: ${NETBSDSRCDIR}/minix/include/arch/${MACHINE_ARCH}/include

procoffsets.h: ${PROCOFFSETSCF} kernel.h proc.h stackframe.h archtypes.h
	${_MKTARGET_CREATE}
	${TOOL_CAT} ${HERE}/${PROCOFFSETSCF} | \
		${TOOL_GENASSYM} -- ${CC} ${CFLAGS:N-Wa,*} \
		${CPPFLAGS} ${PROF} ${${USE_BITCODE:Uno} == "yes":? -fno-lto:} \
		${GENASSYM_CPPFLAGS} >$@.tmp && \
	  mv -f $@.tmp $@

sconst.h: procoffsets.h
