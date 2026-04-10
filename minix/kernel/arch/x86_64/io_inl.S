/*	inl() - Input one dword		(amd64 / SysV ABI rewrite)	*/
/*	unsigned inl(U16_t port);					*/
#include <machine/asm.h>

ENTRY(inl)
	movl	%edi, %edx		/* port (1st arg) */
	inl	%dx			/* read 1 dword into %eax */
	ret
