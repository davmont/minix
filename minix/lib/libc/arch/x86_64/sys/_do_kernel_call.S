/*
 * x86-64 Minix kernel call stub.
 *
 * Kernel SYSCALL convention for kernel calls (from mpx.S):
 *   %rdi = KERVEC_UM  (call-type selector)
 *   %rax = message pointer (the kernel_call message)
 *   syscall
 */
#include <minix/ipcconst.h>
#include <machine/asm.h>

ENTRY(_do_kernel_call_intr)
	mov	%rdi, %rax		/* rax = message pointer (1st C arg) */
	mov	$KERVEC_UM, %rdi	/* rdi = kernel-call selector */
	syscall
	ret
