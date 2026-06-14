/*
 * x86-64 Minix __sigreturn stub.
 * Called by the signal trampoline to return from a signal handler.
 * Jumps to the C sigreturn() function which issues the actual system call.
 */
#include <machine/asm.h>

IMPORT(sigreturn)

ENTRY(__sigreturn)
	/*
	 * At entry RSP = &sf_signum (handler's ret popped sf_ra_sigreturn).
	 * Signal frame layout from RSP:
	 *   RSP+ 0: sf_signum  (int,  4)
	 *   RSP+ 4: sf_code    (int,  4)
	 *   RSP+ 8: sf_scp     (ptr,  8) <- pointer to the saved sigcontext
	 *   RSP+16: sf_fp      (long, 8)
	 *   RSP+24: sf_ra      (long, 8) <- becomes return addr after addq
	 *
	 * Load sf_scp into RDI so that sigreturn(scp) receives the right
	 * pointer, then discard the 24 bytes of frame (sf_signum through sf_fp).
	 */
	movq	8(%rsp), %rdi		/* RDI = sf_scp = &frp->sf_sc */
	addq	$24, %rsp		/* discard sf_signum, sf_code, sf_scp, sf_fp */
#ifndef __PIC__
	jmp	_C_LABEL(sigreturn)
#else
	jmp	PLT(_C_LABEL(sigreturn))
#endif
