/*
 * x86-64 Minix __sigreturn stub.
 * Called by the signal trampoline to return from a signal handler.
 * Jumps to the C sigreturn() function which issues the actual system call.
 */
#include <machine/asm.h>

IMPORT(sigreturn)

ENTRY(__sigreturn)
	addq	$24, %rsp		/* discard signal frame on stack */
#ifndef __PIC__
	jmp	_C_LABEL(sigreturn)
#else
	jmp	PLT(_C_LABEL(sigreturn))
#endif
