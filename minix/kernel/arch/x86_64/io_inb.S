/*	inb() - Input one byte		(amd64 / SysV ABI rewrite)	*/
/*	unsigned inb(U16_t port);					*/
#include <machine/asm.h>

ENTRY(inb)
	xorl	%eax, %eax
	movl	%edi, %edx		/* port (1st arg) */
	inb	%dx			/* read 1 byte into %al */
	ret
