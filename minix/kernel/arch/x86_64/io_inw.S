/*	inw() - Input one word		(amd64 / SysV ABI rewrite)	*/
/*	unsigned inw(U16_t port);					*/
#include <machine/asm.h>

ENTRY(inw)
	xorl	%eax, %eax
	movl	%edi, %edx		/* port (1st arg) */
	inw	%dx			/* read 1 word into %ax */
	ret
