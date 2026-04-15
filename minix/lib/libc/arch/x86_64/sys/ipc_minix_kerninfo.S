/*
 * x86-64 Minix kerninfo IPC stub.
 *
 * Retrieves kernel info via IPC using MINIX_KERNINFO call type.
 * The kernel returns the kerninfo pointer in %rbx.
 *
 * C signature: void ipc_minix_kerninfo(minix_kerninfo_t **kip)
 *   1st arg → %rdi = pointer to store the kerninfo pointer
 */
#include <minix/ipcconst.h>
#include <machine/asm.h>

ENTRY(ipc_minix_kerninfo)
	push	%rbx
	push	%rdi			/* save output pointer */
	xor	%rax, %rax		/* src_dest = 0 */
	xor	%rbx, %rbx		/* call = 0 initially */
	mov	$MINIX_KERNINFO, %rbx	/* rbx = MINIX_KERNINFO */
	xor	%r10, %r10		/* no message pointer */
	mov	$IPCVEC_UM, %rdi	/* rdi = IPC selector */
	syscall
	pop	%rcx			/* rcx = output pointer (saved rdi) */
	mov	%rbx, (%rcx)		/* *kip = kerninfo pointer (from rbx) */
	pop	%rbx
	ret
