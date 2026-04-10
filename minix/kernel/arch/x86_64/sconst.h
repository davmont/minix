#ifndef __SCONST_AMD64_H__
#define __SCONST_AMD64_H__

#include "kernel/const.h"
#include "kernel/procoffsets.h"

/*
 * Offset of the current-process pointer on the kernel stack right after a
 * trap.  On amd64, the CPU pushes SS, RSP, RFLAGS, CS, RIP (5 * 8 bytes)
 * and we additionally push the vector + error code (2 * 8 bytes), giving
 * 7 * 8 = 56 bytes below the saved RBP we push as scratch.
 */
#define CURR_PROC_PTR   56

/*
 * Test whether the interrupt/exception originated in kernel mode.
 * The CS pushed by the CPU is at (displ)(%rsp) from the current stack top.
 * Kernel CS has the lower 2 RPL bits == 0; user CS has RPL == 3.
 */
#define TEST_INT_IN_KERNEL(displ, label)        \
	cmpq    $KERN_CS_SELECTOR, displ(%rsp)  ;\
	je      label                           ;

/*
 * Save all 64-bit general-purpose registers to the process structure pointed
 * to by %rbp.  Caller must ensure %rbp already holds the proc pointer.
 */
#define SAVE_GP_REGS(pptr)              \
	mov     %rax, AXREG(pptr)       ;\
	mov     %rcx, CXREG(pptr)       ;\
	mov     %rdx, DXREG(pptr)       ;\
	mov     %rbx, BXREG(pptr)       ;\
	mov     %rsi, SIREG(pptr)       ;\
	mov     %rdi, DIREG(pptr)       ;\
	mov     %r8,  R8REG(pptr)       ;\
	mov     %r9,  R9REG(pptr)       ;\
	mov     %r10, R10REG(pptr)      ;\
	mov     %r11, R11REG(pptr)      ;\
	mov     %r12, R12REG(pptr)      ;\
	mov     %r13, R13REG(pptr)      ;\
	mov     %r14, R14REG(pptr)      ;\
	mov     %r15, R15REG(pptr)      ;

#define RESTORE_GP_REGS(pptr)           \
	mov     AXREG(pptr),  %rax      ;\
	mov     CXREG(pptr),  %rcx      ;\
	mov     DXREG(pptr),  %rdx      ;\
	mov     BXREG(pptr),  %rbx      ;\
	mov     SIREG(pptr),  %rsi      ;\
	mov     DIREG(pptr),  %rdi      ;\
	mov     R8REG(pptr),  %r8       ;\
	mov     R9REG(pptr),  %r9       ;\
	mov     R10REG(pptr), %r10      ;\
	mov     R11REG(pptr), %r11      ;\
	mov     R12REG(pptr), %r12      ;\
	mov     R13REG(pptr), %r13      ;\
	mov     R14REG(pptr), %r14      ;\
	mov     R15REG(pptr), %r15      ;

/*
 * Save the CPU-pushed interrupt frame (RIP, CS, RFLAGS, RSP, SS) from the
 * stack into the process structure.
 * 'displ' accounts for any extra items on the stack above the frame
 * (e.g., error code + vector = 16 bytes).
 */
#define SAVE_TRAP_CTX(displ, pptr, tmp)                 \
	mov     (0  + displ)(%rsp), tmp                 ;\
	mov     tmp, PCREG(pptr)                         ;\
	mov     (8  + displ)(%rsp), tmp                 ;\
	mov     tmp, CSREG(pptr)                         ;\
	mov     (16 + displ)(%rsp), tmp                 ;\
	mov     tmp, PSWREG(pptr)                        ;\
	mov     (24 + displ)(%rsp), tmp                 ;\
	mov     tmp, SPREG(pptr)                         ;

/*
 * Restore kernel segment registers after an interrupt.
 * In 64-bit mode DS/ES are mostly ignored; we zero FS/GS base via MSRs
 * elsewhere, so this just reloads %ds/%es for compatibility.
 */
#define RESTORE_KERNEL_SEGS             \
	mov     $KERN_DS_SELECTOR, %ax  ;\
	mov     %ax, %ds                ;\
	mov     %ax, %es                ;

/*
 * Full context save macro.
 * 'displ' = bytes of extra data above the CPU frame (vector + errcode = 16).
 * 'trapcode' = KTS_* constant indicating how the kernel was entered.
 *
 * On entry the stack looks like (top = low address):
 *   [displ bytes: vector, errcode]
 *   RIP, CS, RFLAGS, RSP, SS   <- pushed by CPU
 *
 * We push %rbp first to get a scratch register, then load the proc pointer
 * from the top of the kernel stack, save all GPRs into it, pop the saved
 * %rbp back out and store it in BPREG, then save the CPU frame.
 */
#define SAVE_PROCESS_CTX(displ, trapcode)                               \
	cld                                                             ;\
	push    %rbp                                                    ;\
	mov     (CURR_PROC_PTR + 8 + displ)(%rsp), %rbp                ;\
	SAVE_GP_REGS(%rbp)                                              ;\
	movq    $trapcode, P_KERN_TRAP_STYLE(%rbp)                      ;\
	pop     %rsi        /* original rbp */                          ;\
	mov     %rsi, BPREG(%rbp)                                       ;\
	RESTORE_KERNEL_SEGS                                             ;\
	SAVE_TRAP_CTX(displ, %rbp, %rsi)                                ;

/*
 * Clear the IF bit in RFLAGS stored somewhere in memory (e.g. on the stack),
 * so that the flags are loaded with interrupts disabled on iretq.
 */
#define CLEAR_IF(where)                         \
	mov     where, %rax                     ;\
	andq    $~0x200, %rax                   ;\
	mov     %rax, where                     ;

#endif /* __SCONST_AMD64_H__ */
