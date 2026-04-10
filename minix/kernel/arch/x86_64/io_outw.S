/*	outw() - Output one word	(amd64 / SysV ABI rewrite)	*/
/*	void outw(U16_t port, U16_t value);				*/
#include <machine/asm.h>

ENTRY(outw)
	movl	%edi, %edx		/* port  (1st arg) */
	movl	%esi, %eax		/* value (2nd arg) */
	outw	%dx			/* output 1 word from %ax */
	ret
