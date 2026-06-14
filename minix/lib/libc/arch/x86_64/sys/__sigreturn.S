/*
 * x86-64 Minix __sigreturn stub.
 * Reached when a signal handler returns.  The kernel (do_sigsend) stashed the
 * sigcontext pointer in the callee-saved %r15, which the handler and all of
 * its callees must preserve per the amd64 ABI, so it still holds the pointer
 * here regardless of the exact stack offset of the signal frame.
 *
 * The C sigreturn() takes the sigcontext pointer as its first argument (%rdi)
 * and issues the SYS_SIGRETURN call that restores the interrupted context.
 * Force a 16-byte aligned stack so that, after the call's return-address push,
 * sigreturn() sees the (%rsp % 16) == 8 state the amd64 ABI requires for its
 * own SSE stack accesses (sigreturn zeroes a sigset_t with movdqa).
 * sigreturn() does not return on success; if it ever does, trap.
 */
#include <machine/asm.h>

IMPORT(sigreturn)

ENTRY(__sigreturn)
	movq	%r15, %rdi		/* %rdi = sf_scp (preserved in %r15) */
	andq	$-16, %rsp		/* 16-align; the call below makes it %16==8 */
#ifndef __PIC__
	call	_C_LABEL(sigreturn)
#else
	call	PLT(_C_LABEL(sigreturn))
#endif
	ud2				/* sigreturn() should not return */
