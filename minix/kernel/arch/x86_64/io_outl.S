/*	outl() - Output one dword	(amd64 / SysV ABI rewrite)	*/
/*	void outl(U16_t port, u32_t value);				*/
#include <machine/asm.h>

ENTRY(outl)
	movl	%edi, %edx		/* port  (1st arg) */
	movl	%esi, %eax		/* value (2nd arg) */
	outl	%dx			/* output 1 dword from %eax */
	ret
