/*
 * arch_system.c - amd64 system-dependent kernel functions.
 *
 * Adapted from arch/i386/arch_system.c.  Key differences:
 *   - No SYSENTER/SYSEXIT; only SYSCALL/SYSRET.
 *   - restore_user_context() dispatches to syscall or int variant only.
 *   - arch_finish_switch_to_user() writes proc ptr to TSS.RSP0 slot.
 *   - FPU init enables SSE2 (OSFXSR + OSXMMEXCPT in CR4) unconditionally
 *     since all amd64 CPUs support it.
 *   - arch_proc_reset() initialises all 16 GPRs to zero.
 */

#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <machine/cmos.h>
#include <machine/bios.h>
#include <machine/cpu.h>
#include <minix/portio.h>
#include <minix/cpufeature.h>
#include <assert.h>
#include <signal.h>
#include <machine/vm.h>
#include <minix/u64.h>

#include "kernel/kernel.h"
#include "include/archconst.h"
#include "include/arch_proto.h"
#include "glo.h"
#include "serial.h"

#ifdef USE_APIC
#include "apic.h"
#endif

#ifdef USE_ACPI
#include "acpi.h"
#endif

/* k_stacks is set during arch_init() and used by get_k_stack_top(). */
void *k_stacks;

static void ser_debug(int c);
static void ser_dump_vfs(void);

#if !CONFIG_OXPCIE
static void ser_init(void);
#endif

/*===========================================================================*
 *  fpu_init                                                                  *
 *===========================================================================*/
void fpu_init(void)
{
    unsigned short cw, sw;

    fninit();
    sw = fnstsw();
    fnstcw(&cw);

    if ((sw & 0xff) == 0 && (cw & 0x103f) == 0x3f) {
        reg_t cr0, cr4;

        /* All amd64 CPUs have FPU + SSE2, so we enable both flags. */
        cr0 = read_cr0();
        cr0 &= ~(1UL << 2);    /* clear EM (no FPU emulation) */
        cr0 |=  (1UL << 1);    /* set MP */
        cr0 |=  (1UL << 5);    /* set NE (native FP exceptions) */
        write_cr0(cr0);

        cr4 = read_cr4();
        cr4 |= CR4_OSFXSR;     /* enable FXSAVE/FXRSTOR */
        cr4 |= CR4_OSXMMEXCPT; /* enable SSE #XM exceptions */
        write_cr4(cr4);

        get_cpulocal_var(fpu_presence) = 1;
    } else {
        get_cpulocal_var(fpu_presence) = 0;
    }
}

/*===========================================================================*
 *  save_local_fpu                                                            *
 *===========================================================================*/
void save_local_fpu(struct proc *pr, int retain)
{
    char *state = pr->p_seg.fpu_state;

    if (!is_fpu()) return;
    assert(state);

    /* amd64 always has FXSR; use FXSAVE unconditionally. */
    fxsave(state);
    (void)retain;   /* FXSAVE is non-destructive, retain flag irrelevant */
}

/*===========================================================================*
 *  save_fpu                                                                  *
 *===========================================================================*/
void save_fpu(struct proc *pr)
{
#ifdef CONFIG_SMP
    if (cpuid != pr->p_cpu) {
        int stopped = RTS_ISSET(pr, RTS_PROC_STOP);
        smp_schedule_stop_proc_save_ctx(pr);
        if (!stopped)
            RTS_UNSET(pr, RTS_PROC_STOP);
        return;
    }
#endif

    if (get_cpulocal_var(fpu_owner) == pr) {
        disable_fpu_exception();
        save_local_fpu(pr, TRUE);
    }
}

/* FPU state storage: 512 bytes each, 16-byte aligned (FXSAVE requirement). */
static char fpu_state[NR_PROCS][FPU_XFP_SIZE] __aligned(FPUALIGN);

/*===========================================================================*
 *  arch_proc_reset                                                           *
 *===========================================================================*/
void arch_proc_reset(struct proc *pr)
{
    char *v = NULL;
    struct stackframe_s reg;

    assert(pr->p_nr < NR_PROCS);

    if (pr->p_nr >= 0) {
        v = fpu_state[pr->p_nr];
        assert(!((vir_bytes)v % FPUALIGN));
        memset(v, 0, FPU_XFP_SIZE);
    }

    memset(&reg, 0, sizeof(reg));
    reg.psw = iskerneln(pr->p_nr) ? INIT_TASK_PSW : INIT_PSW;

    pr->p_seg.fpu_state = v;

    /* Segment selectors used by this process in user mode. */
    pr->p_reg.cs = USER_CS_SELECTOR;
    pr->p_reg.ss = USER_DS_SELECTOR;
    /* On amd64, DS/ES/FS/GS are not stored in the stackframe_s directly;
     * the hardware only cares about CS/SS for IRETQ. */

    arch_proc_setcontext(pr, &reg, 0, KTS_FULLCONTEXT);
}

/*===========================================================================*
 *  arch_set_secondary_ipc_return                                             *
 *===========================================================================*/
void arch_set_secondary_ipc_return(struct proc *p, u32_t val)
{
    /* On amd64 the secondary IPC return goes in RBX (mirrors i386 EBX). */
    p->p_reg.rbx = val;
}

/*===========================================================================*
 *  restore_fpu                                                               *
 *===========================================================================*/
int restore_fpu(struct proc *pr)
{
    char *state = pr->p_seg.fpu_state;
    assert(state);

    if (!proc_used_fpu(pr)) {
        fninit();
        pr->p_misc_flags |= MF_FPU_INITIALIZED;
    } else {
        if (fxrstor(state))
            return EINVAL;
    }
    return OK;
}

/*===========================================================================*
 *  cpu_enable_features                                                       *
 *===========================================================================*/
/*
 * Enable CPU features that must be set on every CPU (BSP and APs) after
 * cpu_identify() has populated cpu_info[].  Add new per-item blocks here
 * as later phases enable XSAVE, FSGSBASE, PCID, etc.
 */
void cpu_enable_features(void)
{
    u32_t efer_hi, efer_lo;

    /* P1.1 — No-Execute (NX) bit.
     *
     * Set EFER.NXE so that page-table entries with the XD (bit 63) flag
     * actually prevent instruction fetches from data pages.  The bit is
     * defined in archconst.h but was never written to the MSR.
     *
     * Safe unconditionally on x86-64: the NX feature is architecturally
     * guaranteed on all AMD64-compatible CPUs (CPUID 0x80000001 EDX bit 20
     * is always set on any CPU that implements the AMD64 long-mode spec).
     *
     * EFER is a 32-bit MSR; hi word is reserved/zero.
     */
    ia32_msr_read(AMD_MSR_EFER, &efer_hi, &efer_lo);
    if (!(efer_lo & AMD_EFER_NXE))
        ia32_msr_write(AMD_MSR_EFER, efer_hi, efer_lo | AMD_EFER_NXE);
}

/*===========================================================================*
 *  cpu_identify                                                              *
 *===========================================================================*/
void cpu_identify(void)
{
    u32_t eax, ebx, ecx, edx;
    unsigned cpu = cpuid;

    eax = 0;
    _cpuid(&eax, &ebx, &ecx, &edx);

    if (ebx == INTEL_CPUID_GEN_EBX && ecx == INTEL_CPUID_GEN_ECX &&
            edx == INTEL_CPUID_GEN_EDX)
        cpu_info[cpu].vendor = CPU_VENDOR_INTEL;
    else if (ebx == AMD_CPUID_GEN_EBX && ecx == AMD_CPUID_GEN_ECX &&
             edx == AMD_CPUID_GEN_EDX)
        cpu_info[cpu].vendor = CPU_VENDOR_AMD;
    else
        cpu_info[cpu].vendor = CPU_VENDOR_UNKNOWN;

    if (eax == 0) return;

    eax = 1;
    _cpuid(&eax, &ebx, &ecx, &edx);

    cpu_info[cpu].family = (eax >> 8) & 0xf;
    if (cpu_info[cpu].family == 0xf)
        cpu_info[cpu].family += (eax >> 20) & 0xff;
    cpu_info[cpu].model = (eax >> 4) & 0xf;
    if (cpu_info[cpu].model == 0xf || cpu_info[cpu].model == 0x6)
        cpu_info[cpu].model += ((eax >> 16) & 0xf) << 4;
    cpu_info[cpu].stepping  = eax & 0xf;
    cpu_info[cpu].flags[0]  = ecx;
    cpu_info[cpu].flags[1]  = edx;
}

/*===========================================================================*
 *  arch_init                                                                 *
 *===========================================================================*/
void arch_init(void)
{
    k_stacks = (void *)&k_stacks_start;
    assert(!((vir_bytes)k_stacks % K_STACK_SIZE));

#ifndef CONFIG_SMP
    tss_init(0, get_k_stack_top(0));
#endif

#if !CONFIG_OXPCIE
    ser_init();
#endif

#ifdef USE_ACPI
    acpi_init();
#endif

#if defined(USE_APIC) && !defined(CONFIG_SMP)
    if (config_no_apic) {
        DEBUGBASIC(("APIC disabled, using legacy PIC\n"));
    } else if (!apic_single_cpu_init()) {
        DEBUGBASIC(("APIC not present, using legacy PIC\n"));
    }
#endif

    /* Reserve BIOS memory regions. */
    cut_memmap(&kinfo, BIOS_MEM_BEGIN, BIOS_MEM_END);
    cut_memmap(&kinfo, BASE_MEM_TOP,   UPPER_MEM_END);
}

/*===========================================================================*
 *  arch_do_syscall                                                           *
 *===========================================================================*/
void arch_do_syscall(struct proc *proc)
{
    assert(proc == get_cpulocal_var(proc_ptr));
    assert(proc->p_misc_flags & MF_SC_DEFER);
    proc->p_reg.retreg =
        do_ipc(proc->p_defer.r1, proc->p_defer.r2, proc->p_defer.r3);
}

/*===========================================================================*
 *  arch_finish_switch_to_user                                                *
 *===========================================================================*/
struct proc *arch_finish_switch_to_user(void)
{
    struct proc *p;
    reg_t *stk;

    /*
     * Write the current proc pointer to the top of the kernel stack so that
     * SYSCALL / interrupt stubs can retrieve it cheaply (*(RSP0) == proc).
     */
#ifdef CONFIG_SMP
    stk = (reg_t *)(uintptr_t)tss[cpuid].rsp0;
#else
    stk = (reg_t *)(uintptr_t)tss[0].rsp0;
#endif

    p = get_cpulocal_var(proc_ptr);
    *stk = (reg_t)(uintptr_t)p;

    /* Ensure IF is set so the process runs with interrupts enabled. */
    p->p_reg.psw |= IF_MASK;

    /* Honour single-step tracing. */
    if (p->p_misc_flags & MF_STEP)
        p->p_reg.psw |=  TRACEBIT;
    else
        p->p_reg.psw &= ~TRACEBIT;

    return p;
}

/*===========================================================================*
 *  arch_proc_setcontext                                                      *
 *===========================================================================*/
void arch_proc_setcontext(struct proc *p, struct stackframe_s *state,
                          int isuser, int trap_style)
{
    if (isuser) {
        state->psw = (state->psw & X86_FLAGS_USER) |
                     (p->p_reg.psw & ~X86_FLAGS_USER);
    }

    assert(sizeof(p->p_reg) == sizeof(*state));
    if (state != &p->p_reg)
        memcpy(&p->p_reg, state, sizeof(*state));

    p->p_misc_flags |= MF_CONTEXT_SET;

    if (!(p->p_rts_flags)) {
        printf("WARNING: setting full context of runnable process\n");
        print_proc(p);
        util_stacktrace();
    }
    if (p->p_seg.p_kern_trap_style == KTS_NONE)
        printf("WARNING: setting full context of out-of-kernel process\n");
    p->p_seg.p_kern_trap_style = trap_style;
}

/*===========================================================================*
 *  restore_user_context                                                      *
 *===========================================================================*/
void restore_user_context(struct proc *p)
{
    int trap_style = p->p_seg.p_kern_trap_style;

    p->p_seg.p_kern_trap_style = KTS_NONE;

    if (trap_style == KTS_SYSCALL) {
        restore_user_context_syscall(p);
        NOT_REACHABLE;
    }

    switch (trap_style) {
    case KTS_NONE:
        panic("no entry trap style known");
    case KTS_INT_HARD:
    case KTS_INT_UM:
    case KTS_FULLCONTEXT:
    case KTS_INT_ORIG:
        restore_user_context_int(p);
        NOT_REACHABLE;
    default:
        panic("unknown trap style recorded: %d", trap_style);
        NOT_REACHABLE;
    }

    NOT_REACHABLE;
}

/*===========================================================================*
 *  fpu_sigcontext                                                            *
 *===========================================================================*/
void fpu_sigcontext(struct proc *pr, struct sigframe_sigcontext *fr,
                    struct sigcontext *sc)
{
    int fp_error;

    /* amd64 always uses the FXSAVE area. */
    fp_error = sc->sc_fpu_state.xfp_regs.fp_status &
               ~sc->sc_fpu_state.xfp_regs.fp_control;

    if      (fp_error & 0x001) fr->sf_code = FPE_FLTINV;
    else if (fp_error & 0x004) fr->sf_code = FPE_FLTDIV;
    else if (fp_error & 0x008) fr->sf_code = FPE_FLTOVF;
    else if (fp_error & 0x012) fr->sf_code = FPE_FLTUND;
    else if (fp_error & 0x020) fr->sf_code = FPE_FLTRES;
    else                       fr->sf_code = 0;
}

reg_t arch_get_sp(struct proc *p) { return p->p_reg.sp; }

/*===========================================================================*
 *  Serial debug helpers (identical to i386 version)                          *
 *===========================================================================*/

static void ser_dump_vfs(void)
{
#if DEBUG_SERIAL
    mini_notify(proc_addr(KERNEL), VFS_PROC_NR);
#endif
}

static void ser_dump_queue_cpu(unsigned cpu)
{
    int q;
    struct proc **rdy_head = get_cpu_var(cpu, run_q_head);

    for (q = 0; q < NR_SCHED_QUEUES; q++) {
        struct proc *p;
        if (rdy_head[q]) {
            printf("%2d: ", q);
            for (p = rdy_head[q]; p; p = p->p_nextready)
                printf("%s/%d  ", p->p_name, p->p_endpoint);
            printf("\n");
        }
    }
}

static void ser_dump_queues(void)
{
#ifdef CONFIG_SMP
    unsigned cpu;
    printf("--- run queues ---\n");
    for (cpu = 0; cpu < ncpus; cpu++) {
        printf("CPU %d:\n", cpu);
        ser_dump_queue_cpu(cpu);
    }
#else
    ser_dump_queue_cpu(0);
#endif
}

void do_ser_debug(void)
{
    u8_t c, lsr;

    lsr = inb(COM1_LSR);
    if (!(lsr & LSR_DR)) return;
    c = inb(COM1_RBR);
    ser_debug(c);
}

static void ser_debug(const int c)
{
    serial_debug_active = 1;

    switch (c) {
    case 'Q':
        minix_shutdown(0);
        NOT_REACHABLE;
    case '1':
        ser_dump_proc();
        break;
    case '2':
        ser_dump_queues();
        break;
    case '5':
        ser_dump_vfs();
        break;
    }

    serial_debug_active = 0;
}

#if !CONFIG_OXPCIE
static void ser_init(void)
{
    unsigned char lcr;
    unsigned divisor;

    if (kinfo.serial_debug_baud <= 0) return;

    lcr = LCR_8BIT | LCR_1STOP | LCR_NPAR;
    outb(COM1_LCR, lcr | LCR_DLAB);

    divisor = UART_BASE_FREQ / kinfo.serial_debug_baud;
    if (divisor < 1)     divisor = 1;
    if (divisor > 65535) divisor = 65535;

    outb(COM1_DLL, divisor & 0xff);
    outb(COM1_DLM, (divisor >> 8) & 0xff);
    outb(COM1_LCR, lcr);
}
#endif

#if SPROFILE
int arch_init_profile_clock(const u32_t freq)
{
    int r;
    outb(RTC_INDEX, RTC_REG_A);
    outb(RTC_IO,    RTC_A_DV_OK | freq);
    outb(RTC_INDEX, RTC_REG_B);
    r = inb(RTC_IO);
    outb(RTC_INDEX, RTC_REG_B);
    outb(RTC_IO,    r | RTC_B_PIE);
    outb(RTC_INDEX, RTC_REG_C);
    inb(RTC_IO);
    return CMOS_CLOCK_IRQ;
}

void arch_stop_profile_clock(void)
{
    int r;
    outb(RTC_INDEX, RTC_REG_B);
    r = inb(RTC_IO);
    outb(RTC_INDEX, RTC_REG_B);
    outb(RTC_IO,    r & ~RTC_B_PIE);
}

void arch_ack_profile_clock(void)
{
    outb(RTC_INDEX, RTC_REG_C);
    inb(RTC_IO);
}
#endif
