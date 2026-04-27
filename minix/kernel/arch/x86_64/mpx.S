/*
 * mpx.S - amd64 interrupt and exception entry stubs.
 *
 * In 64-bit mode the CPU automatically pushes SS, RSP, RFLAGS, CS, RIP
 * (and optionally an error code) on the stack when an exception/interrupt
 * fires.  Each stub pushes a zero error-code placeholder (if the CPU does
 * not) and the vector number, then jumps to the common handler.
 *
 * The stack is 16-byte aligned by the ABI before a CALL instruction.  The
 * CPU adds 5 * 8 = 40 bytes during the trap, which is not aligned to 16.
 * We add 8 more bytes ourselves (push $vector) to make total pushed bytes
 * a multiple of 16 before calling C code.
 *
 * Key amd64 differences vs. i386:
 *   - 16 GPRs (RAX-R15) instead of 8; all must be saved/restored.
 *   - IRETQ (not IRET) to return from interrupts.
 *   - No PUSHA/POPA; we save registers individually.
 *   - 64-bit IDT gate entries (16 bytes each, 64-bit handler offset).
 *   - SYSCALL/SYSRET replaces INT 0x80/SYSENTER.
 */

#include "kernel/kernel.h"

#include <machine/vm.h>
#include <machine/asm.h>
#include <machine/interrupt.h>
#include <minix/config.h>
#include <minix/const.h>
#include <minix/ipcconst.h>
#include <minix/com.h>

#include "include/archconst.h"
#include "kernel/const.h"
#include "kernel/proc.h"
#include "sconst.h"
#include "include/arch_proto.h"

/*===========================================================================*
 *  Hardware interrupt handlers (IRQ 0-15 via 8259 PIC)                      *
 *===========================================================================*/

#define PIC_IRQ_HANDLER(irq)                                            \
    movq    $irq, %rdi                                                  ;\
    call    _C_LABEL(irq_handle)                                        ;

/*
 * Master PIC interrupts (IRQ 0-7).
 * When the interrupt fires from user space we save context, stop the
 * process cycle counter, dispatch the handler, send EOI, and jump to
 * switch_to_user().  When nested (kernel was already running) we save
 * a minimal frame, handle it, and return with IRETQ.
 *
 * TEST_INT_IN_KERNEL checks CS (at RSP+8 after the CPU-pushed frame with no
 * extra stub items):
 *   RSP+0 = RIP, RSP+8 = CS, RSP+16 = RFLAGS, [RSP+24=RSP, RSP+32=SS for ring-3]
 *
 * In the kernel-nested path (0:) we push 9 caller-saved regs (72 bytes).
 * For a ring-0 interrupt (3 CPU items): RFLAGS is at RSP+(9+2)*8 = 11*8
 * after those pushes, so CLEAR_IF uses offset 11*8.
 */
#define hwint_master(irq)                                               \
    TEST_INT_IN_KERNEL(8, 0f)                                           ;\
    SAVE_PROCESS_CTX(0, KTS_INT_HARD)                                   ;\
    push    %rbp                                                        ;\
    xor     %rbp, %rbp                                                  ;\
    call    _C_LABEL(context_stop)                                      ;\
    pop     %rbp                                                        ;\
    PIC_IRQ_HANDLER(irq)                                                ;\
    movb    $END_OF_INT, %al                                            ;\
    outb    $INT_CTL                                                    ;\
    jmp     _C_LABEL(switch_to_user)                                    ;\
0:                                                                      ;\
    push    %rax                                                        ;\
    push    %rcx                                                        ;\
    push    %rdx                                                        ;\
    push    %rsi                                                        ;\
    push    %rdi                                                        ;\
    push    %r8                                                         ;\
    push    %r9                                                         ;\
    push    %r10                                                        ;\
    push    %r11                                                        ;\
    call    _C_LABEL(context_stop_idle)                                 ;\
    PIC_IRQ_HANDLER(irq)                                                ;\
    call    _C_LABEL(bkl_unlock_after_intr)                             ;\
    movb    $END_OF_INT, %al                                            ;\
    outb    $INT_CTL                                                    ;\
    CLEAR_IF(11*8(%rsp))    /* clear IF in saved RFLAGS (ring-0: 9+2=11 slots) */;\
    pop     %r11                                                        ;\
    pop     %r10                                                        ;\
    pop     %r9                                                         ;\
    pop     %r8                                                         ;\
    pop     %rdi                                                        ;\
    pop     %rsi                                                        ;\
    pop     %rdx                                                        ;\
    pop     %rcx                                                        ;\
    pop     %rax                                                        ;\
    iretq                                                               ;

/* Slave PIC interrupts (IRQ 8-15) — same but also send EOI to slave. */
#define hwint_slave(irq)                                                \
    TEST_INT_IN_KERNEL(8, 0f)                                           ;\
    SAVE_PROCESS_CTX(0, KTS_INT_HARD)                                   ;\
    push    %rbp                                                        ;\
    xor     %rbp, %rbp                                                  ;\
    call    _C_LABEL(context_stop)                                      ;\
    pop     %rbp                                                        ;\
    PIC_IRQ_HANDLER(irq)                                                ;\
    movb    $END_OF_INT, %al                                            ;\
    outb    $INT_CTL                                                    ;\
    outb    $INT2_CTL                                                   ;\
    jmp     _C_LABEL(switch_to_user)                                    ;\
0:                                                                      ;\
    push    %rax                                                        ;\
    push    %rcx                                                        ;\
    push    %rdx                                                        ;\
    push    %rsi                                                        ;\
    push    %rdi                                                        ;\
    push    %r8                                                         ;\
    push    %r9                                                         ;\
    push    %r10                                                        ;\
    push    %r11                                                        ;\
    call    _C_LABEL(context_stop_idle)                                 ;\
    PIC_IRQ_HANDLER(irq)                                                ;\
    call    _C_LABEL(bkl_unlock_after_intr)                             ;\
    movb    $END_OF_INT, %al                                            ;\
    outb    $INT_CTL                                                    ;\
    outb    $INT2_CTL                                                   ;\
    CLEAR_IF(11*8(%rsp))                                                ;\
    pop     %r11                                                        ;\
    pop     %r10                                                        ;\
    pop     %r9                                                         ;\
    pop     %r8                                                         ;\
    pop     %rdi                                                        ;\
    pop     %rsi                                                        ;\
    pop     %rdx                                                        ;\
    pop     %rcx                                                        ;\
    pop     %rax                                                        ;\
    iretq                                                               ;

ENTRY(hwint00)  hwint_master(0)
ENTRY(hwint01)  hwint_master(1)
ENTRY(hwint02)  hwint_master(2)
ENTRY(hwint03)  hwint_master(3)
ENTRY(hwint04)  hwint_master(4)
ENTRY(hwint05)  hwint_master(5)
ENTRY(hwint06)  hwint_master(6)
ENTRY(hwint07)  hwint_master(7)

ENTRY(hwint08)  hwint_slave(8)
ENTRY(hwint09)  hwint_slave(9)
ENTRY(hwint10)  hwint_slave(10)
ENTRY(hwint11)  hwint_slave(11)
ENTRY(hwint12)  hwint_slave(12)
ENTRY(hwint13)  hwint_slave(13)
ENTRY(hwint14)  hwint_slave(14)
ENTRY(hwint15)  hwint_slave(15)

/*===========================================================================*
 *  SYSCALL entry (Phase 4)                                                   *
 *===========================================================================*/
/*
 * The SYSCALL instruction does NOT switch stacks automatically.  The kernel
 * must switch to the per-CPU kernel stack manually.  The CPU saves RIP in
 * RCX and RFLAGS in R11; interrupts are disabled.
 *
 * Register convention (mirrors i386 SYSCALL usage):
 *   RDI = call type (IPCVEC_UM / KERVEC_UM)
 *   RAX, RBX, R10 = syscall parameters
 *   RCX = user RIP (saved by CPU)
 *   R11 = user RFLAGS (saved by CPU)
 *   RSP = user stack pointer (NOT automatically switched)
 */
ENTRY(syscall_entry)
    /*
     * Switch to the per-CPU kernel stack.  The current CPU's proc pointer
     * sits at the top of the kernel stack; we grab the stack pointer from
     * the per-cpu TSS RSP0 field via the GS-base if SMP, or directly via
     * k_percpu_stacks[0] for the simple case.
     *
     * For the initial single-CPU bring-up we keep it simple: reload RSP
     * from the BSP's k_percpu_stacks[0].
     */
    swapgs                          /* get kernel GS base */
    mov     %rsp, %gs:0             /* stash user RSP in per-cpu scratch */
    mov     %gs:8, %rsp             /* load kernel RSP */

    /* Retrieve the current process pointer. */
    mov     (%rsp), %rbp            /* proc ptr at top of kernel stack */

    /* Record that we entered via SYSCALL. */
    movq    $KTS_SYSCALL, P_KERN_TRAP_STYLE(%rbp)

    /* Save user RSP and RIP. */
    mov     %gs:0, %rsi
    mov     %rsi, SPREG(%rbp)       /* user RSP */
    mov     %rcx, PCREG(%rbp)       /* user RIP (RCX after SYSCALL) */
    mov     %r11, PSWREG(%rbp)      /* user RFLAGS (R11 after SYSCALL) */

    swapgs                          /* restore user GS base */

    /* Dispatch on call type. */
    cmp     $IPCVEC_UM, %rdi
    jz      ipc_entry_common

    cmp     $KERVEC_UM, %rdi
    jz      kernel_call_entry_common

    /* Unknown vector — return error. */
    movq    $-1, AXREG(%rbp)
    push    %rbp
    call    restore_user_context_syscall

/*===========================================================================*
 *  IPC entry points                                                          *
 *===========================================================================*/
ENTRY(ipc_entry_softint_orig)
    SAVE_PROCESS_CTX(0, KTS_INT_ORIG)
    jmp     ipc_entry_common

ENTRY(ipc_entry_softint_um)
    SAVE_PROCESS_CTX(0, KTS_INT_UM)
    jmp     ipc_entry_common

ENTRY(ipc_entry_common)
    push    %rbp

    /* Pass syscall arguments (RBX=call, RAX=src_dest, R10=msg_ptr). */
    mov     %rbx, %rdi
    mov     %rax, %rsi
    mov     %r10, %rdx

    push    %rbp
    xor     %rbp, %rbp
    call    _C_LABEL(context_stop)
    pop     %rbp

    call    _C_LABEL(do_ipc)

    pop     %rsi
    mov     %rax, AXREG(%rsi)
    jmp     _C_LABEL(switch_to_user)

/*===========================================================================*
 *  Kernel call entry                                                         *
 *===========================================================================*/
ENTRY(kernel_call_entry_orig)
    SAVE_PROCESS_CTX(0, KTS_INT_ORIG)
    jmp     kernel_call_entry_common

ENTRY(kernel_call_entry_um)
    SAVE_PROCESS_CTX(0, KTS_INT_UM)
    jmp     kernel_call_entry_common

ENTRY(kernel_call_entry_common)
    push    %rbp

    /* RAX = message pointer (first argument in 64-bit SysV ABI: RDI). */
    mov     %rax, %rdi

    push    %rbp
    xor     %rbp, %rbp
    call    _C_LABEL(context_stop)
    pop     %rbp

    call    _C_LABEL(kernel_call)

    pop     %rbp
    jmp     _C_LABEL(switch_to_user)

/*===========================================================================*
 *  Context restore                                                           *
 *===========================================================================*/

/*
 * restore_user_context_syscall(struct proc *rp)
 * Return to user via SYSRET.  RIP must be in RCX, RFLAGS in R11.
 * Only clobbers the caller-saved registers needed for SYSRET.
 */
ENTRY(restore_user_context_syscall)
    mov     %rdi, %rbp          /* proc ptr passed as first argument */

    /* Restore user RFLAGS -> R11 (SYSRET uses R11). */
    mov     PSWREG(%rbp), %r11

    /* Restore user RIP -> RCX (SYSRET uses RCX). */
    mov     PCREG(%rbp), %rcx

    /* Restore return value. */
    mov     AXREG(%rbp), %rax

    /* Restore user stack. */
    mov     SPREG(%rbp), %rsp

    /* Restore remaining GPRs. */
    RESTORE_GP_REGS(%rbp)
    mov     BPREG(%rbp), %rbp

    sysretq

/*
 * restore_user_context_int(struct proc *rp)
 * Return to user via IRETQ.  Used when the process was entered via an
 * interrupt gate (INT n or hardware interrupt from user mode).
 */
ENTRY(restore_user_context_int)
    mov     %rdi, %rbp          /* proc ptr */

    /* Reconstruct the IRETQ frame: SS, RSP, RFLAGS, CS, RIP */
    push    $USER_DS_SELECTOR           /* SS */
    mov     SPREG(%rbp), %rax
    push    %rax                        /* RSP */
    mov     PSWREG(%rbp), %rax
    push    %rax                        /* RFLAGS */
    push    $USER_CS_SELECTOR           /* CS */
    mov     PCREG(%rbp), %rax
    push    %rax                        /* RIP */

    /* Restore segment registers. */
    mov     $USER_DS_SELECTOR, %ax
    mov     %ax, %ds
    mov     %ax, %es

    /* Restore return value. */
    mov     AXREG(%rbp), %rax

    /* Restore all other GPRs. */
    RESTORE_GP_REGS(%rbp)
    mov     BPREG(%rbp), %rbp

    iretq

/*===========================================================================*
 *  Exception entry (common dispatcher)                                       *
 *===========================================================================*/
/*
 * Each exception stub has already pushed [error_code, vector] on the stack.
 * The CPU has pushed [RIP, CS, RFLAGS, RSP, SS] below that.
 * Stack layout on entry here (top = low address):
 *   vector      <- we pushed
 *   error_code  <- CPU or we pushed 0
 *   RIP
 *   CS
 *   RFLAGS
 *   RSP
 *   SS
 */
/* After the exception stubs push errcode + vector (2 × 8 = 16 bytes),
 * the CPU-pushed CS is at RSP+24 (vector=RSP+0, errcode=RSP+8, RIP=RSP+16, CS=RSP+24). */
.balign 16
exception_entry:
    TEST_INT_IN_KERNEL(24, exception_entry_nested)

exception_entry_from_user:
    /* displ=16: skip over [vector, errcode] already on stack */
    SAVE_PROCESS_CTX(16, KTS_INT_HARD)

    push    %rbp
    xor     %rbp, %rbp
    call    _C_LABEL(context_stop)
    pop     %rbp

    /*
     * Call exception_handler(is_nested=0, frame_ptr).
     * The exception frame starts at the CPU-pushed items; point RSI at it.
     */
    mov     %rsp, %rsi          /* pointer to exception frame (incl. vec+err) */
    xor     %rdi, %rdi          /* is_nested = 0 */
    call    _C_LABEL(exception_handler)

    jmp     _C_LABEL(switch_to_user)

exception_entry_nested:
    /* Save only caller-saved regs; the kernel stack is already active. */
    push    %rax
    push    %rcx
    push    %rdx
    push    %rsi
    push    %rdi
    push    %r8
    push    %r9
    push    %r10
    push    %r11

    lea     9*8(%rsp), %rsi     /* point to [vector, errcode, RIP, ...] */
    mov     $1, %rdi            /* is_nested = 1 */
    call    _C_LABEL(exception_handler)

    pop     %r11
    pop     %r10
    pop     %r9
    pop     %r8
    pop     %rdi
    pop     %rsi
    pop     %rdx
    pop     %rcx
    pop     %rax

    /* Remove [errcode, vector] and resume. */
    add     $16, %rsp
    iretq

/*===========================================================================*
 *  Exception stubs (push 0 or real error code, then vector, jump to common) *
 *===========================================================================*/

#define EXCEPTION_ERR_CODE(vector)      \
    pushq   $vector                     ;\
    jmp     exception_entry             ;

#define EXCEPTION_NO_ERR_CODE(vector)   \
    pushq   $0                          ;\
    EXCEPTION_ERR_CODE(vector)          ;

LABEL(divide_error)         EXCEPTION_NO_ERR_CODE(0)
LABEL(single_step_exception) EXCEPTION_NO_ERR_CODE(1)
LABEL(nmi)                  EXCEPTION_NO_ERR_CODE(2)
LABEL(breakpoint_exception) EXCEPTION_NO_ERR_CODE(3)
LABEL(overflow)             EXCEPTION_NO_ERR_CODE(4)
LABEL(bounds_check)         EXCEPTION_NO_ERR_CODE(5)
LABEL(inval_opcode)         EXCEPTION_NO_ERR_CODE(6)
LABEL(copr_not_available)   EXCEPTION_NO_ERR_CODE(7)
LABEL(double_fault)         EXCEPTION_ERR_CODE(8)      /* CPU pushes error code */
LABEL(copr_seg_overrun)     EXCEPTION_NO_ERR_CODE(9)
LABEL(inval_tss)            EXCEPTION_ERR_CODE(10)
LABEL(segment_not_present)  EXCEPTION_ERR_CODE(11)
LABEL(stack_exception)      EXCEPTION_ERR_CODE(12)
LABEL(general_protection)   EXCEPTION_ERR_CODE(13)
LABEL(page_fault)           EXCEPTION_ERR_CODE(14)
LABEL(copr_error)           EXCEPTION_NO_ERR_CODE(16)
LABEL(alignment_check)      EXCEPTION_ERR_CODE(17)
LABEL(machine_check)        EXCEPTION_NO_ERR_CODE(18)
LABEL(simd_exception)       EXCEPTION_NO_ERR_CODE(19)

/*===========================================================================*
 *  BSS: per-CPU SYSCALL scratch area                                        *
 *===========================================================================*/
.data
    .align 8
/* SYSCALL scratch: [user_rsp, kernel_rsp] per cpu.  Only BSP slot needed
 * until SMP is brought up. */
LABEL(syscall_scratch)
    .space  2 * 8 * CONFIG_MAX_CPUS
