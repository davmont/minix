/*
 * usermapped_glo_ipc.S - x86-64 user-mapped IPC stubs.
 *
 * These routines live in the .usermapped_glo section so the kernel can
 * map them read-only into every user process's address space.
 *
 * System V AMD64 ABI: integer args in RDI, RSI, RDX, RCX, R8, R9.
 *
 * Kernel IPC register convention (from mpx.S/ipc_entry_common):
 *   RBX = IPC call opcode  (SEND, RECEIVE, SENDREC, ...)
 *   RAX = src/dest endpoint
 *   R10 = message pointer
 *
 * Kernel call convention (from mpx.S/kernel_call_entry_common):
 *   RAX = message pointer
 *
 * For SYSCALL dispatch (syscall_entry), the kernel checks RDI:
 *   RDI = IPCVEC_UM  → IPC
 *   RDI = KERVEC_UM  → kernel_call
 *
 * SYSCALL clobbers RCX (saved RIP) and R11 (saved RFLAGS); all other
 * registers survive across the trap as they are saved in the proc struct.
 */

#include <minix/ipcconst.h>
#include <machine/asm.h>

/* Redefine _ENTRY to place code in .usermapped_glo */
#undef _ENTRY
#define _ENTRY(x) \
	_ALIGN_TEXT; .globl x; .type x,@function; x:

.section .usermapped_glo, "wax", @progbits

/*
 * Macro: IPCFUNC name, opcode, vec
 *
 * Generates softint and syscall variants of an IPC stub.
 *
 * User args (send/sendrec/sendnb/notify family):
 *   RDI = src/dest endpoint
 *   RSI = message pointer
 */
#define IPCFUNC(name, opcode, vec)					\
ENTRY(usermapped_ ## name ## _softint)					;\
	mov	%rdi, %rax	/* RAX = endpoint */			;\
	mov	%rsi, %r10	/* R10 = message */			;\
	movq	$opcode, %rbx	/* RBX = call opcode */			;\
	int	$vec		/* trap into kernel */			;\
	ret								;\
ENTRY(usermapped_ ## name ## _syscall)					;\
	mov	%rdi, %rax	/* RAX = endpoint */			;\
	mov	%rsi, %r10	/* R10 = message */			;\
	movq	$opcode, %rbx	/* RBX = call opcode */			;\
	movq	$vec, %rdi	/* RDI = selector (clobbers arg!) */	;\
	syscall			/* trap into kernel */			;\
	ret

/*
 * receive needs special handling: the kernel returns IPC status in RBX,
 * which must be written to *status_ptr (third argument, RDX).
 * RBX is a callee-saved register, so we preserve it on the stack.
 */
#define RECVFUNC(name, opcode, vec)					\
ENTRY(usermapped_ ## name ## _softint)					;\
	push	%rbx		/* save callee-saved RBX */		;\
	push	%rdx		/* save status_ptr */			;\
	mov	%rdi, %rax	/* RAX = src endpoint */		;\
	mov	%rsi, %r10	/* R10 = message */			;\
	movq	$opcode, %rbx	/* RBX = RECEIVE */			;\
	int	$vec		/* trap; on return RBX = ipc_status */	;\
	pop	%rcx		/* RCX = status_ptr */			;\
	mov	%rbx, (%rcx)	/* *status_ptr = ipc_status */		;\
	pop	%rbx		/* restore RBX */			;\
	ret								;\
ENTRY(usermapped_ ## name ## _syscall)					;\
	push	%rbx		/* save callee-saved RBX */		;\
	push	%rdx		/* save status_ptr */			;\
	mov	%rdi, %rax	/* RAX = src endpoint */		;\
	mov	%rsi, %r10	/* R10 = message */			;\
	movq	$opcode, %rbx	/* RBX = RECEIVE */			;\
	movq	$vec, %rdi	/* RDI = IPCVEC_UM selector */		;\
	syscall			/* trap; on return RBX = ipc_status */	;\
	pop	%rcx		/* RCX = status_ptr */			;\
	mov	%rbx, (%rcx)	/* *status_ptr = ipc_status */		;\
	pop	%rbx		/* restore RBX */			;\
	ret

/*
 * senda(asynmsg_t *table, size_t count):
 *   Kernel expects: RBX=SENDA, RAX=count, R10=table
 */
#define SENDAFUNC(name, opcode, vec)					\
ENTRY(usermapped_ ## name ## _softint)					;\
	mov	%rsi, %rax	/* RAX = count */			;\
	mov	%rdi, %r10	/* R10 = table */			;\
	movq	$opcode, %rbx	/* RBX = SENDA */			;\
	int	$vec							;\
	ret								;\
ENTRY(usermapped_ ## name ## _syscall)					;\
	mov	%rsi, %rax	/* RAX = count */			;\
	mov	%rdi, %r10	/* R10 = table */			;\
	movq	$opcode, %rbx	/* RBX = SENDA */			;\
	movq	$vec, %rdi	/* RDI = selector */			;\
	syscall								;\
	ret

/*
 * do_kernel_call(message *m_ptr):
 *   Kernel expects: RAX = message pointer, trapped via KERVEC_UM
 */
#define KERNCALLFUNC(name, vec)						\
ENTRY(usermapped_ ## name ## _softint)					;\
	mov	%rdi, %rax	/* RAX = msg */				;\
	int	$vec							;\
	ret								;\
ENTRY(usermapped_ ## name ## _syscall)					;\
	mov	%rdi, %rax	/* RAX = msg */				;\
	movq	$vec, %rdi	/* RDI = KERVEC_UM */			;\
	syscall								;\
	ret

IPCFUNC(send,      SEND,    IPCVEC_UM)
RECVFUNC(receive,  RECEIVE, IPCVEC_UM)
IPCFUNC(sendrec,   SENDREC, IPCVEC_UM)
IPCFUNC(sendnb,    SENDNB,  IPCVEC_UM)
IPCFUNC(notify,    NOTIFY,  IPCVEC_UM)
SENDAFUNC(senda,   SENDA,   IPCVEC_UM)
KERNCALLFUNC(do_kernel_call, KERVEC_UM)
