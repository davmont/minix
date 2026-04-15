/*
 * x86-64: _do_kernel_call_intr.S
 * On x86-64 there is no separate interrupt-based kernel call;
 * the SYSCALL instruction is used for all kernel calls.
 * This file just re-exports the same implementation.
 */
#include <minix/ipcconst.h>
#include <machine/asm.h>

ENTRY(_do_kernel_call_intr)
	mov	%rdi, %rax		/* rax = message pointer (1st C arg) */
	mov	$KERVEC_UM, %rdi	/* rdi = kernel-call selector */
	syscall
	ret
