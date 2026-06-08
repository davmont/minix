/*
 * x86-64 Minix ucontext implementation.
 *
 * getcontext(ucontext_t *ucp)  — save current context into *ucp
 * setcontext(const ucontext_t *ucp) — restore context from *ucp
 *
 * Ported from the i386 version; uses the same PM/kernel interface but
 * with x86-64 register names and the System V AMD64 ABI.
 */
#include <machine/asm.h>
#include <ucontextoffsets.h>

IMPORT(getuctx)
IMPORT(setuctx)
IMPORT(resumecontext)

	.globl	_C_LABEL(__errno)

/*
 * int getcontext(ucontext_t *ucp)
 *   Save the current user context into *ucp.
 *   1st arg (ucp) → %rdi on entry.
 */
ENTRY(getcontext)
ENTRY(_getcontext)
	/* Check for NULL pointer */
	test	%rdi, %rdi
	jnz	3f
	PIC_PROLOGUE
	call	PIC_PLT(_C_LABEL(__errno))
	PIC_EPILOGUE
	movl	$EFAULT, (%rax)
	xor	%eax, %eax
	dec	%eax			/* return -1 */
	ret

3:	/* Check whether FPU and signal-mask save can be skipped */
	mov	UC_FLAGS(%rdi), %eax
	and	$[_UC_IGNFPU|_UC_IGNSIGM], %eax
	cmp	$[_UC_IGNFPU|_UC_IGNSIGM], %eax
	jz	5f			/* skip getuctx if both ignored */
	PIC_PROLOGUE
	/*
	 * Preserve ucp across the call with a SINGLE push.  On entry rsp is
	 * 8 mod 16 (the call to getcontext pushed the return address onto a
	 * 16-aligned stack); one 8-byte push makes it 0 mod 16, satisfying the
	 * System V AMD64 requirement that rsp be 16-byte aligned at a call.
	 * The previous code pushed twice (staying 8 mod 16), so getuctx ran on a
	 * misaligned stack and faulted on the first movaps in it or its callees.
	 */
	push	%rdi
	call	PIC_PLT(_C_LABEL(getuctx))
	pop	%rdi
	PIC_EPILOGUE

5:
	/* Save caller's return address (popped off stack) into ucp->uc_mcontext */
	pop	PC(%rdi)		/* save real return address */
	mov	%rsp, SP(%rdi)		/* save stack pointer (now past ret addr) */

	/* Save callee-saved GP registers */
	mov	%rbp, RBP(%rdi)
	mov	%rbx, RBX(%rdi)
	mov	%r12, R12(%rdi)
	mov	%r13, R13(%rdi)
	mov	%r14, R14(%rdi)
	mov	%r15, R15(%rdi)

	/* Save argument registers so a resumed context can see them */
	mov	%rdi, RDI(%rdi)
	mov	%rsi, RSI(%rdi)
	mov	%rdx, RDX(%rdi)
	mov	%rcx, RCX(%rdi)
	mov	%r8,  R8(%rdi)
	mov	%r9,  R9(%rdi)

	/* getcontext returns 0 on success */
	xor	%eax, %eax
	mov	%rax, RAX(%rdi)		/* save 0 as return value in context */

	/* Stamp magic number */
	movl	$MCF_MAGIC, MAGIC(%rdi)

	xor	%eax, %eax
	jmp	*PC(%rdi)		/* return via saved PC */


/*
 * int setcontext(const ucontext_t *ucp)
 *   Restore context from *ucp.  Does not return on success.
 *
 * The public name 'setcontext' is a WEAK alias of the strong '_setcontext'
 * (NetBSD pattern).  libpthread's pthread_specific.c does
 * __strong_alias(setcontext, pthread_setcontext) to preserve the pthread
 * private pointer across context switches; making libc's setcontext weak
 * lets that strong override win at link time instead of colliding.
 */
WEAK_ALIAS(setcontext, _setcontext)
ENTRY(_setcontext)
	/* Check for NULL */
	test	%rdi, %rdi
	jnz	1f
	PIC_PROLOGUE
	call	PIC_PLT(_C_LABEL(__errno))
	PIC_EPILOGUE
	movl	$EFAULT, (%rax)
	xor	%eax, %eax
	dec	%eax
	ret

1:	/* Check magic */
	cmpl	$MCF_MAGIC, MAGIC(%rdi)
	jz	2f
	PIC_PROLOGUE
	call	PIC_PLT(_C_LABEL(__errno))
	PIC_EPILOGUE
	movl	$EINVAL, (%rax)
	xor	%eax, %eax
	dec	%eax
	ret

2:	/* Restore FPU / signal mask via PM if needed */
	mov	UC_FLAGS(%rdi), %eax
	and	$[_UC_IGNFPU|_UC_IGNSIGM], %eax
	cmp	$[_UC_IGNFPU|_UC_IGNSIGM], %eax
	jz	4f
	PIC_PROLOGUE
	/* Single push: 16-align rsp for the call (see getcontext above). */
	push	%rdi
	call	PIC_PLT(_C_LABEL(setuctx))
	pop	%rdi
	PIC_EPILOGUE

4:	/* Restore GP registers and jump to saved PC */
	mov	RBP(%rdi), %rbp
	mov	RBX(%rdi), %rbx
	mov	R12(%rdi), %r12
	mov	R13(%rdi), %r13
	mov	R14(%rdi), %r14
	mov	R15(%rdi), %r15
	mov	RSI(%rdi), %rsi
	mov	RDX(%rdi), %rdx
	mov	RCX(%rdi), %rcx
	mov	R8(%rdi),  %r8
	mov	R9(%rdi),  %r9
	mov	RAX(%rdi), %rax
	mov	SP(%rdi),  %rsp
	push	PC(%rdi)		/* push return address */
	mov	RDI(%rdi), %rdi		/* restore rdi last */
	ret				/* jump to saved PC */

/*
 * void ctx_start(void)
 *   Called when makecontext's func returns.
 *   R12 holds the ucontext_t * (set by makecontext, callee-saved across func).
 *   Calls resumecontext(ucp) to switch to the next context or exit.
 */
ENTRY(ctx_start)
	mov	%r12, %rdi		/* ucp = first arg to resumecontext */
	PIC_PROLOGUE
	call	PIC_PLT(_C_LABEL(resumecontext))
	PIC_EPILOGUE
	/* resumecontext does not return */
	ud2
