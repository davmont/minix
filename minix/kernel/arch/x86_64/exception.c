/*
 * exception.c - amd64 exception handler.
 *
 * Adapted from arch/i386/exception.c.  Key differences:
 *   - exception_frame uses rip/rflags/rsp (64-bit) instead of eip/eflags/esp.
 *   - Stack-trace walks 64-bit frame pointers (RBP chains).
 *   - No SYSENTER path; only SYSCALL/SYSRET.
 *   - CR0.TS / CLTS for FPU lazy-context still applies on amd64.
 */

#include "kernel/kernel.h"
#include "include/arch_proto.h"
#include <signal.h>
#include <string.h>
#include <assert.h>
#include <machine/vm.h>
#ifdef USE_WATCHDOG
#include "kernel/watchdog.h"
#endif

struct ex_s {
    char *msg;
    int   signum;
};

static struct ex_s ex_data[] = {
    { "Divide error",               SIGFPE  },  /*  0 */
    { "Debug exception",            SIGTRAP },  /*  1 */
    { "Nonmaskable interrupt",      SIGBUS  },  /*  2 */
    { "Breakpoint",                 SIGEMT  },  /*  3 */
    { "Overflow",                   SIGFPE  },  /*  4 */
    { "Bounds check",               SIGFPE  },  /*  5 */
    { "Invalid opcode",             SIGILL  },  /*  6 */
    { "Coprocessor not available",  SIGFPE  },  /*  7 */
    { "Double fault",               SIGBUS  },  /*  8 */
    { "Coprocessor segment overrun",SIGSEGV },  /*  9 */
    { "Invalid TSS",                SIGSEGV },  /* 10 */
    { "Segment not present",        SIGSEGV },  /* 11 */
    { "Stack exception",            SIGSEGV },  /* 12 */
    { "General protection",         SIGSEGV },  /* 13 */
    { "Page fault",                 SIGSEGV },  /* 14 */
    { NULL,                         SIGILL  },  /* 15 – reserved */
    { "Coprocessor error",          SIGFPE  },  /* 16 */
    { "Alignment check",            SIGBUS  },  /* 17 */
    { "Machine check",              SIGBUS  },  /* 18 */
    { "SIMD exception",             SIGFPE  },  /* 19 */
};


static void proc_stacktrace_execute(struct proc *whichproc, reg_t v_rbp,
                                    reg_t pc);

/*===========================================================================*
 *  pagefault                                                                 *
 *===========================================================================*/
static void pagefault(struct proc *pr, struct exception_frame *frame,
                      int is_nested)
{
    int in_physcopy = 0, in_memset = 0;
    reg_t pagefaultcr2;
    message m_pagefault;
    int err;

    pagefaultcr2 = read_cr2();

    in_physcopy = (frame->rip > (vir_bytes)phys_copy) &&
                  (frame->rip < (vir_bytes)phys_copy_fault);

    in_memset   = (frame->rip > (vir_bytes)phys_memset) &&
                  (frame->rip < (vir_bytes)memset_fault);

    if ((is_nested || iskernelp(pr)) &&
            get_cpulocal_var(catch_pagefaults) && (in_physcopy || in_memset)) {
        if (is_nested) {
            if (in_physcopy) {
                assert(!in_memset);
                frame->rip = (reg_t)phys_copy_fault_in_kernel;
            } else {
                frame->rip = (reg_t)memset_fault_in_kernel;
            }
        } else {
            pr->p_reg.pc     = (reg_t)phys_copy_fault;
            pr->p_reg.retreg = pagefaultcr2;
        }
        return;
    }

    if (is_nested) {
        printf("pagefault in kernel at rip 0x%lx address 0x%lx\n",
               (unsigned long)frame->rip, (unsigned long)pagefaultcr2);
        panic("nested pagefault");
    }

    if (pr->p_endpoint == VM_PROC_NR) {
        printf("pagefault for VM on CPU %d, "
               "rip=0x%lx addr=0x%lx flags=0x%lx is_nested=%d\n",
               cpuid, (unsigned long)pr->p_reg.pc,
               (unsigned long)pagefaultcr2,
               (unsigned long)frame->errcode, is_nested);
        proc_stacktrace(pr);
        panic("pagefault in VM");
    }

    {
        /* Detect a process page-faulting on the same address repeatedly
         * (would indicate VM never cleared RTS_PAGEFAULT or never mapped
         * the page).  Print details every Nth repeat to expose the loop
         * without flooding. */
        static endpoint_t last_ep = NONE;
        static reg_t last_addr = 0;
        static unsigned same_count = 0;
        static unsigned printed = 0;
        if (pr->p_endpoint == last_ep && pagefaultcr2 == last_addr) {
            same_count++;
            if ((same_count == 5 || same_count == 50 ||
                 (same_count % 500) == 0) && printed < 20) {
                printf("PFLOOP: proc=%d addr=0x%lx err=0x%lx repeat=%u "
                       "rip=0x%lx\n",
                       pr->p_endpoint, (unsigned long)pagefaultcr2,
                       (unsigned long)frame->errcode, same_count,
                       (unsigned long)frame->rip);
                printed++;
            }
        } else {
            last_ep = pr->p_endpoint;
            last_addr = pagefaultcr2;
            same_count = 1;
        }
    }

    RTS_SET(pr, RTS_PAGEFAULT);

    m_pagefault.m_source  = pr->p_endpoint;
    m_pagefault.m_type    = VM_PAGEFAULT;
    m_pagefault.VPF_ADDR  = pagefaultcr2;
    m_pagefault.VPF_FLAGS = frame->errcode;

    if ((err = mini_send(pr, VM_PROC_NR, &m_pagefault, FROM_KERNEL)))
        panic("pagefault: mini_send returned %d\n", err);
}

/*===========================================================================*
 *  inkernel_disaster                                                         *
 *===========================================================================*/
static void inkernel_disaster(struct proc *saved_proc,
                              struct exception_frame *frame,
                              struct ex_s *ep, int is_nested)
{
#if USE_SYSDEBUG
    if (ep) {
        if (ep->msg == NULL)
            printf("\nIntel-reserved exception %d\n", (int)frame->vector);
        else
            printf("\n%s\n", ep->msg);
    }

    printf("cpu %d is_nested=%d\n", cpuid, is_nested);
    printf("vec=%d errcode=0x%lx rip=0x%lx cs=0x%lx rflags=0x%lx rsp=0x%lx\n",
           (int)frame->vector,
           (unsigned long)frame->errcode,
           (unsigned long)frame->rip,
           (unsigned long)frame->cs,
           (unsigned long)frame->rflags,
           (unsigned long)frame->rsp);

    if (saved_proc) {
        printf("scheduled: process %d (%s) pc=0x%lx\n",
               saved_proc->p_endpoint, saved_proc->p_name,
               (unsigned long)saved_proc->p_reg.pc);
        proc_stacktrace(saved_proc);
        panic("Unhandled kernel exception");
    }

    panic("exception in kernel while booting, no saved_proc yet");
#endif
}

/*===========================================================================*
 *  exception_handler                                                         *
 *===========================================================================*/
void exception_handler(int is_nested, struct exception_frame *frame)
{
    struct ex_s *ep;
    struct proc *saved_proc;

    BOOT_VERBOSE(printf(
        "exc: vec=%lu err=0x%lx rip=0x%lx cs=0x%lx is_nested=%d cr2=0x%lx\n",
        (unsigned long)frame->vector, (unsigned long)frame->errcode,
        (unsigned long)frame->rip, (unsigned long)frame->cs, is_nested,
        (unsigned long)read_cr2()));

    saved_proc = get_cpulocal_var(proc_ptr);
    ep = (frame->vector < (reg_t)(sizeof(ex_data) / sizeof(ex_data[0])))
         ? &ex_data[frame->vector] : NULL;

    /* NMI: route to watchdog if enabled, otherwise treat as spurious. */
    if (frame->vector == 2) {
#ifdef USE_WATCHDOG
        nmi_watchdog_handler((struct nmi_frame *)frame);
#else
        printf("got spurious NMI\n");
#endif
        return;
    }

    if (is_nested) {
        /*
         * Kernel-mode copy_msg fault: redirect RIP to the failure label.
         */
        if (((void *)frame->rip >= (void *)copy_msg_to_user &&
             (void *)frame->rip <= (void *)__copy_msg_to_user_end) ||
            ((void *)frame->rip >= (void *)copy_msg_from_user &&
             (void *)frame->rip <= (void *)__copy_msg_from_user_end)) {
            switch (frame->vector) {
            case PAGE_FAULT_VECTOR:
            case PROTECTION_VECTOR:
                frame->rip = (reg_t)__user_copy_msg_pointer_failure;
                return;
            default:
                panic("Copy via user pointer failed unexpectedly!");
            }
        }

        /* FPU state restore fault (FXRSTOR path). */
        if ((void *)frame->rip >= (void *)fxrstor &&
            (void *)frame->rip <= (void *)__fxrstor_end) {
            frame->rip = (reg_t)__frstor_failure;
            return;
        }

        /* FPU state restore fault (XRSTOR path). */
        if ((void *)frame->rip >= (void *)xrstor_asm &&
            (void *)frame->rip <= (void *)__xrstor_end) {
            frame->rip = (reg_t)__xrstor_failure;
            return;
        }

        /* Debug trap while tracing through a SYSCALL entry. */
        if (frame->vector == DEBUG_VECTOR &&
            (saved_proc->p_reg.psw & TRACEBIT) &&
            saved_proc->p_seg.p_kern_trap_style == KTS_NONE) {
            frame->rflags &= ~TRACEBIT;
            return;
        }
    }

    if (frame->vector == PAGE_FAULT_VECTOR) {
        pagefault(saved_proc, frame, is_nested);
        return;
    }

    if (is_nested == 0 && !iskernelp(saved_proc)) {
        /* #NM (Device Not Available): lazy FPU context switch. */
        if (frame->vector == COPROC_NOT_VECTOR) {
            copr_not_available_handler();
            NOT_REACHABLE;
        }
        printf("exception: proc=%s/%d vec=%d (%s) rip=0x%lx rsp=0x%lx err=0x%lx sig=%d\n",
               saved_proc->p_name, saved_proc->p_endpoint,
               (int)frame->vector,
               (ep && ep->msg) ? ep->msg : "?",
               (unsigned long)frame->rip,
               (unsigned long)frame->rsp,
               (unsigned long)frame->errcode,
               ep ? ep->signum : SIGILL);
        if (frame->vector == 6) {
            printf("  #UD cr0=0x%lx cr4=0x%lx\n",
                   (unsigned long)read_cr0(), (unsigned long)read_cr4());
        }
        cause_sig(proc_nr(saved_proc), ep ? ep->signum : SIGILL);
        return;
    }

    inkernel_disaster(saved_proc, frame, ep, is_nested);
    panic("return from inkernel_disaster");
}

/*===========================================================================*
 *  proc_stacktrace_execute  (64-bit RBP chain walk)                         *
 *===========================================================================*/
#if USE_SYSDEBUG
static void proc_stacktrace_execute(struct proc *whichproc, reg_t v_rbp,
                                    reg_t pc)
{
    reg_t v_hbp;
    int iskernel = iskernelp(whichproc);
    int n = 0;

    printf("%-8.8s %6d 0x%lx ",
           whichproc->p_name, whichproc->p_endpoint, (unsigned long)pc);

#define PRCOPY(pr, pv, v, n) \
    (iskernel ? (memcpy((char *)(v), (char *)(pv), (n)), OK) : \
     data_copy((pr)->p_endpoint, (pv), KERNEL, (vir_bytes)(v), (n)))

    while (v_rbp) {
        reg_t v_pc;

        if (PRCOPY(whichproc, v_rbp, &v_hbp, sizeof(v_hbp)) != OK) {
            printf("(v_rbp 0x%lx ?)", (unsigned long)v_rbp);
            break;
        }
        if (PRCOPY(whichproc, v_rbp + sizeof(v_pc), &v_pc,
                   sizeof(v_pc)) != OK) {
            printf("(v_pc 0x%lx ?)", (unsigned long)(v_rbp + sizeof(v_pc)));
            break;
        }
        printf("0x%lx ", (unsigned long)v_pc);
        if (v_hbp != 0 && v_hbp <= v_rbp) {
            printf("(hbp 0x%lx ?)", (unsigned long)v_hbp);
            break;
        }
        v_rbp = v_hbp;
        if (n++ > 50) {
            printf("(truncated after %d steps) ", n);
            break;
        }
    }
    printf("\n");
}
#endif /* USE_SYSDEBUG */

/*===========================================================================*
 *  proc_stacktrace                                                           *
 *===========================================================================*/
void proc_stacktrace(struct proc *whichproc)
{
    reg_t use_rbp;

    if (whichproc->p_seg.p_kern_trap_style == KTS_NONE)
        printf("WARNING: stacktrace of running process\n");

    switch (whichproc->p_seg.p_kern_trap_style) {
    case KTS_SYSCALL:
    {
        /*
         * SYSCALL entry does not save RBP into p_reg automatically.
         * Read it from the user stack (16-byte offset matches the
         * usermapped IPC stub convention).
         */
        reg_t sp = whichproc->p_reg.sp;
        if (data_copy(whichproc->p_endpoint, sp + 16,
                      KERNEL, (vir_bytes)&use_rbp,
                      sizeof(use_rbp)) != OK) {
            printf("stacktrace: copy failed\n");
            return;
        }
        break;
    }
    default:
        use_rbp = whichproc->p_reg.rbp;
        break;
    }

#if USE_SYSDEBUG
    proc_stacktrace_execute(whichproc, use_rbp, whichproc->p_reg.pc);
#endif
}

/*===========================================================================*
 *  FPU exception helpers                                                     *
 *===========================================================================*/
void enable_fpu_exception(void)
{
    reg_t cr0 = read_cr0();
    /* CR0.TS (bit 3) — trigger #NM on next FPU instruction */
    if (!(cr0 & (1UL << 3)))
        write_cr0(cr0 | (1UL << 3));
}

void disable_fpu_exception(void)
{
    clts();     /* clear CR0.TS */
}
