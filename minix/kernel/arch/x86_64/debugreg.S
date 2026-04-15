/*
 * debugreg.S - x86-64 debug register accessors.
 *
 * SysV AMD64 ABI: first arg in RDI, return value in RAX.
 * Debug registers are 64-bit on x86-64.
 */
#include <machine/asm.h>

#define LD_ST_REG(reg)							\
ENTRY(ld_##reg)								;\
	mov	%rdi, %reg						;\
	ret								;\
									;\
ENTRY(st_##reg)								;\
	mov	%reg, %rax						;\
	ret

/*
 * void ld_dr0(phys_bytes value)
 * phys_bytes st_dr0(void)
 */
LD_ST_REG(dr0)
LD_ST_REG(dr1)
LD_ST_REG(dr2)
LD_ST_REG(dr3)
LD_ST_REG(dr6)
LD_ST_REG(dr7)
