/*
 * x86-64 Minix IPC assembly stubs.
 *
 * Kernel SYSCALL convention (from mpx.S):
 *   %rdi = IPCVEC_UM  (call-type selector for dispatch)
 *   %rax = src_dest   (endpoint)
 *   %rbx = call       (SEND / RECEIVE / SENDREC / NOTIFY / …)
 *   %r10 = msg_ptr    (message pointer)
 *   syscall
 *
 * User-space C calling convention (System V AMD64 ABI):
 *   1st arg → %rdi  (src_dest or dst)
 *   2nd arg → %rsi  (message pointer)
 *   3rd arg → %rdx  (status pointer for SENDREC)
 *
 * %rcx and %r11 are clobbered by SYSCALL/SYSRET; we save them if needed.
 * %rbx is callee-saved; we save/restore it around the syscall.
 */
#include <minix/ipcconst.h>
#include <machine/asm.h>

ENTRY(_ipc_send_intr)
	push	%rbx
	mov	%rdi, %rax		/* rax = dst (src_dest) */
	mov	$SEND, %rbx		/* rbx = call type */
	mov	%rsi, %r10		/* r10 = message pointer */
	mov	$IPCVEC_UM, %rdi	/* rdi = IPC selector */
	syscall
	pop	%rbx
	ret

ENTRY(_ipc_receive_intr)
	push	%rbx
	mov	%rdi, %rax		/* rax = src (src_dest) */
	mov	$RECEIVE, %rbx		/* rbx = call type */
	mov	%rsi, %r10		/* r10 = message pointer */
	mov	$IPCVEC_UM, %rdi	/* rdi = IPC selector */
	syscall
	/* store status (returned in %rbx by kernel) into *status_ptr */
	mov	%rbx, (%rdx)
	pop	%rbx
	ret

ENTRY(_ipc_sendrec_intr)
	push	%rbx
	mov	%rdi, %rax		/* rax = src_dest */
	mov	$SENDREC, %rbx		/* rbx = call type */
	mov	%rsi, %r10		/* r10 = message pointer */
	mov	$IPCVEC_UM, %rdi	/* rdi = IPC selector */
	syscall
	pop	%rbx
	ret

ENTRY(_ipc_notify_intr)
	push	%rbx
	mov	%rdi, %rax		/* rax = dst */
	mov	$NOTIFY, %rbx		/* rbx = call type */
	xor	%r10, %r10		/* no message */
	mov	$IPCVEC_UM, %rdi	/* rdi = IPC selector */
	syscall
	pop	%rbx
	ret

ENTRY(_ipc_sendnb_intr)
	push	%rbx
	mov	%rdi, %rax		/* rax = dst */
	mov	$SENDNB, %rbx		/* rbx = call type */
	mov	%rsi, %r10		/* r10 = message pointer */
	mov	$IPCVEC_UM, %rdi	/* rdi = IPC selector */
	syscall
	pop	%rbx
	ret

ENTRY(_ipc_senda_intr)
	push	%rbx
	mov	%rsi, %rax		/* rax = tabcount (2nd arg) */
	mov	$SENDA, %rbx		/* rbx = call type */
	mov	%rdi, %r10		/* r10 = msgtab pointer (1st arg) */
	mov	$IPCVEC_UM, %rdi	/* rdi = IPC selector */
	syscall
	pop	%rbx
	ret
