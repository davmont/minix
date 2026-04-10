/*	outb() - Output one byte	(amd64 / SysV ABI rewrite)	*/
/*	void outb(U16_t port, U8_t value);				*/
#include <machine/asm.h>

ENTRY(outb)
	movl	%edi, %edx		/* port  (1st arg) */
	movl	%esi, %eax		/* value (2nd arg) */
	outb	%dx			/* output 1 byte from %al */
	ret
