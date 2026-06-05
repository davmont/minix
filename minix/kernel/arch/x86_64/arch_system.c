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

/*
 * XSAVE state: set once during BSP fpu_init(), then read-only.
 *
 *  use_xsave     — non-zero when XSAVE/XRSTOR are available (CR4.OSXSAVE set)
 *  use_xsaveopt  — non-zero when XSAVEOPT is also available (faster saves)
 *  xsave_area_size — bytes required per process; between FPU_XFP_SIZE and
 *                    FPU_XSAVE_MAX_SIZE, queried from CPUID leaf 0xD
 */
static int    use_xsave    = 0;
static int    use_xsaveopt = 0;
static size_t xsave_area_size = FPU_XFP_SIZE;

/* Exported so that do_fork / do_sigsend / do_sigreturn can use the right size. */
size_t fpu_get_save_size(void) { return xsave_area_size; }

/*
 * P2.1 — FSGSBASE.
 * Set once in cpu_enable_features().  When non-zero, RDFSBASE/WRFSBASE/
 * RDGSBASE/WRGSBASE instructions are available (and CR4.FSGSBASE is set).
 */
int use_fsgsbase = 0;

/*
 * P2.4 — PCID (Process-Context Identifiers).
 * use_pcid: non-zero once CR4.PCIDE has been set on the BSP.
 * next_pcid: rolling counter; wraps at PCID_MAX (4094, reserving 0 for kernel
 *            and 4095 as a sentinel).  Access is single-threaded during boot;
 *            at runtime only arch_proc_reset() advances it while the BKL is
 *            held, so no additional lock is needed.
 */
int use_pcid   = 0;
#define PCID_KERNEL  0u
#define PCID_MAX     4094u
static u16_t next_pcid = 1;

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
        u32_t eax, ebx, ecx, edx;

        /* All amd64 CPUs have FPU + SSE2; enable FXSAVE and SSE exceptions. */
        cr0 = read_cr0();
        cr0 &= ~(1UL << 2);    /* clear EM */
        cr0 |=  (1UL << 1);    /* set MP */
        cr0 |=  (1UL << 5);    /* set NE */
        write_cr0(cr0);

        cr4 = read_cr4();
        cr4 |= CR4_OSFXSR;
        cr4 |= CR4_OSXMMEXCPT;

        /*
         * P1.2 — XSAVE detection.
         *
         * CPUID leaf 1, ECX bit 26 reports hardware XSAVE support.
         * We additionally need to set CR4.OSXSAVE to tell the CPU the OS
         * will manage the extended state, then re-read CPUID leaf 0xD to
         * find out the actual save-area size for the currently enabled XCR0
         * components (XCR0 defaults to 0x3 = x87 + SSE after reset, but we
         * set it to -1ULL to enable all features the CPU supports).
         */
        eax = 1; ebx = ecx = edx = 0;
        _cpuid(&eax, &ebx, &ecx, &edx);

        if (ecx & CPUID1_ECX_XSAVE) {
            u32_t d1_eax, d1_ebx, d1_ecx, d1_edx;
            u32_t d0_eax, d0_ebx, d0_ecx, d0_edx;

            /* Enable OS XSAVE support in CR4 first. */
            cr4 |= CR4_OSXSAVE;
            write_cr4(cr4);

            /*
             * Expand XCR0 to all features the CPU advertises.
             * CPUID leaf 0xD, subleaf 0, EAX:EDX reports the valid XCR0 bits.
             * We set XCR0 = that mask so that all components are managed.
             */
            d0_eax = 0xD; d0_ecx = 0;
            _cpuid(&d0_eax, &d0_ebx, &d0_ecx, &d0_edx);
            /* XSETBV: write d0_edx:d0_eax into XCR0 (index 0). */
            __asm__ __volatile__(
                "xsetbv"
                :
                : "c" (0),          /* XCR index 0 */
                  "a" (d0_eax),     /* low 32 bits */
                  "d" (d0_edx)      /* high 32 bits */
            );

            /*
             * Re-query CPUID leaf 0xD, subleaf 0 NOW — after xsetbv has
             * expanded XCR0.  CPUID.0xD.0.EBX reports the save-area size
             * for the *current* XCR0, so querying before xsetbv would have
             * given the default (x87+SSE only = 512 B) rather than the full
             * size needed for AVX, AVX-512, etc.
             */
            d0_eax = 0xD; d0_ecx = 0;
            _cpuid(&d0_eax, &d0_ebx, &d0_ecx, &d0_edx);

            /*
             * Query CPUID leaf 0xD, subleaf 1 for XSAVEOPT/XSAVEC support.
             */
            d1_eax = 0xD; d1_ecx = 1;
            _cpuid(&d1_eax, &d1_ebx, &d1_ecx, &d1_edx);

            use_xsaveopt  = (d1_eax & CPUIDD1_EAX_XSAVEOPT) ? 1 : 0;

            /* Leaf 0xD subleaf 0, EBX = size of XSAVE area for current XCR0. */
            xsave_area_size = d0_ebx;
            if (xsave_area_size < FPU_XFP_SIZE)
                xsave_area_size = FPU_XFP_SIZE;
            if (xsave_area_size > FPU_XSAVE_MAX_SIZE) {
                printf("fpu_init: XSAVE area %zu > max %d, capping\n",
                    xsave_area_size, FPU_XSAVE_MAX_SIZE);
                xsave_area_size = FPU_XSAVE_MAX_SIZE;
            }

            use_xsave = 1;
            printf("fpu_init: XSAVE enabled, area=%zu bytes%s\n",
                xsave_area_size, use_xsaveopt ? " (XSAVEOPT)" : "");
        } else {
            write_cr4(cr4);     /* write without CR4_OSXSAVE */
        }

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

    if (use_xsave) {
        /*
         * XSAVEOPT skips saving state components that have not been modified
         * since the last XRSTOR — significantly faster for processes that do
         * not use AVX.  Fall back to plain XSAVE when XSAVEOPT is absent.
         */
        if (use_xsaveopt)
            xsaveopt_asm(state);
        else
            xsave_asm(state);
    } else {
        fxsave(state);
    }

    (void)retain;   /* XSAVE and FXSAVE are both non-destructive */
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

/*
 * FPU state storage.
 *
 * Each slot is FPU_XSAVE_MAX_SIZE bytes so it can accommodate any XSAVE area
 * size reported by CPUID leaf 0xD at runtime.  The alignment is 64 bytes as
 * required by XSAVE (FXSAVE only needs 16; 64 satisfies both).
 *
 * Only the first xsave_area_size bytes of each slot are written/read;
 * the rest are zero-initialised at boot and never touched.
 */
static char fpu_state[NR_PROCS][FPU_XSAVE_MAX_SIZE] __aligned(FPUALIGN);

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
        /*
         * Zero the entire save area.  When XSAVE is active this also clears
         * the XSAVE header (bytes 512–575): XSTATE_BV=0 means "no components
         * saved", which causes XRSTOR to initialise all enabled components to
         * their architectural reset values — exactly what we want for a new
         * process.
         */
        memset(v, 0, xsave_area_size);
    }

    memset(&reg, 0, sizeof(reg));
    reg.psw = iskerneln(pr->p_nr) ? INIT_TASK_PSW : INIT_PSW;

    pr->p_seg.fpu_state = v;

    /*
     * P2.4 — assign a unique PCID for user processes when PCID is active.
     * Kernel processes (p_nr < 0) use PCID_KERNEL (0).
     * The counter wraps at PCID_MAX; on wrap we skip 0 to preserve the
     * kernel reservation.  A full TLB invalidation for the recycled PCID
     * is not needed here because arch_proc_reset always pairs with a fresh
     * page-table assignment (p_cr3 is set later by VM), so stale TLB
     * entries for the old owner of this PCID will never match.
     */
    if (use_pcid && pr->p_nr >= 0) {
        pr->p_seg.p_pcid = next_pcid;
        if (++next_pcid > PCID_MAX)
            next_pcid = 1;
    } else {
        pr->p_seg.p_pcid = PCID_KERNEL;
    }

    /* No TLS thread pointer yet; userland sets %fs base later via WRFSBASE. */
    pr->p_seg.p_fsbase = 0;

    /* Segment selectors for user-mode processes.  These go in 'reg' so they
     * survive the memcpy inside arch_proc_setcontext — setting them directly
     * in pr->p_reg would be overwritten by that copy. */
    if (!iskerneln(pr->p_nr)) {
        reg.cs = USER_CS_SELECTOR;
        reg.ss = USER_DS_SELECTOR;
    }

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
 *  __switch_address_space                                                    *
 *===========================================================================*/
/*
 * Switch the CPU to the address space of process p.
 *
 * P2.4: when use_pcid is active the PCID is OR-ed into bits 11:0 of CR3.
 * We do NOT set bit 63 (the no-flush bit) so that the TLB entries for this
 * PCID are invalidated on every switch.  This is required for correct
 * pagefault handling: without the flush, stale read-only TLB entries cached
 * before a COW fault survive the switch back and the process faults again in
 * an infinite loop.  When use_pcid is not active this degenerates to a plain
 * CR3 write (with the same-CR3 skip optimisation).
 */
void __switch_address_space(struct proc *p, struct proc **ptproc)
{
    reg_t new_cr3 = (reg_t)p->p_seg.p_cr3;

    if (!new_cr3)
        return;

    if (use_pcid) {
        /*
         * OR in the PCID (bits 11:0 of CR3).  Do NOT set bit 63 (no-flush):
         * we need the TLB flushed for this PCID on every switch so that
         * stale read-only TLB entries from before a COW pagefault are
         * invalidated before the process is resumed.  Without this flush the
         * process would see the old protected entry and fault again in an
         * infinite loop.
         */
        new_cr3 |= (reg_t)p->p_seg.p_pcid;
        write_cr3(new_cr3);
    } else {
        reg_t cur_cr3 = read_cr3();
        if (new_cr3 == cur_cr3)
            return;
        write_cr3(new_cr3);
    }

    *ptproc = p;
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
        if (use_xsave) {
            if (xrstor_asm(state))
                return EINVAL;
        } else {
            if (fxrstor(state))
                return EINVAL;
        }
    }
    return OK;
}

/*===========================================================================*
 *  cpu_enable_features                                                       *
 *===========================================================================*/
/*
 * Enable CPU features that must be set on every CPU (BSP and APs) after
 * cpu_identify() has populated cpu_info[].  Called from main.c (BSP) and
 * from the AP bring-up path via smp_init().
 */
void cpu_enable_features(void)
{
    u32_t efer_hi, efer_lo;
    reg_t cr4;

    /* P1.1 — No-Execute (NX) bit.
     *
     * Set EFER.NXE so that page-table entries with the XD (bit 63) flag
     * actually prevent instruction fetches from data pages.
     *
     * Safe unconditionally on x86-64: NX is architecturally guaranteed on
     * all AMD64-compatible CPUs (CPUID 0x80000001 EDX bit 20 always set).
     */
    ia32_msr_read(AMD_MSR_EFER, &efer_hi, &efer_lo);
    if (!(efer_lo & AMD_EFER_NXE))
        ia32_msr_write(AMD_MSR_EFER, efer_hi, efer_lo | AMD_EFER_NXE);

    cr4 = read_cr4();

    /* P2.1 — FSGSBASE.
     *
     * Setting CR4.FSGSBASE (bit 16) allows user-mode code to read/write the
     * FS and GS base registers directly with RDFSBASE/WRFSBASE/RDGSBASE/
     * WRGSBASE — avoiding the MSR path.  The kernel can also use these
     * faster instructions for TLS base setup.
     *
     * The feature bit is _CPUF_X86_FSGSBASE (CPUID leaf 7, EBX bit 0).
     * We set use_fsgsbase on the BSP and propagate to APs: APs set the CR4
     * bit unconditionally if the BSP already set use_fsgsbase.
     */
    if (_cpufeature(_CPUF_X86_FSGSBASE)) {
        cr4 |= CR4_FSGSBASE;
        use_fsgsbase = 1;
    }

    /* P2.4 — PCID (Process-Context Identifiers).
     *
     * CR4.PCIDE (bit 17) enables the PCID mechanism: the bottom 12 bits of
     * CR3 become the current PCID.  Writing CR3 with bit 63 set suppresses
     * the TLB flush for that address space, avoiding full TLB shootdowns on
     * context switches to processes whose entries are still cached.
     *
     * Precondition: CR4.PCIDE requires CR4.PAE (always on in long mode).
     * We only enable it on the BSP; APs mirror the BSP's use_pcid flag.
     *
     * Note: once PCIDE is enabled, ALL CR3 writes must include a valid PCID
     * in bits 11:0.  __switch_address_space() handles this in C.
     */
    if (_cpufeature(_CPUF_X86_PCID)) {
        cr4 |= CR4_PCIDE;
        use_pcid = 1;
    }

    write_cr4(cr4);
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

/* Diagnostic: dump 16 bytes at the RSDT-discovery sentinel address.  Tracks
 * ACPI-table corruption regressions during arch_init. */
#define DBGRSDT(label) BOOT_VERBOSE({ \
    const volatile unsigned char *b = \
        (const volatile unsigned char *) \
        (((char *)0xffff800000000000UL) + 0x3ffe2335UL); \
    int i; \
    printf("DBGRSDT @ " label ":"); \
    for (i = 0; i < 16; i++) printf(" %02x", b[i]); \
    printf("\n"); \
})

#ifdef USE_ACPI
    acpi_init();
    DBGRSDT("post-acpi_init");
    /* Carve ACPI tables out of the memmap so the page allocator doesn't
     * hand their physical pages to processes and clobber the table
     * contents before the userspace ACPI service starts. */
    acpi_reserve_tables();
    DBGRSDT("post-reserve");
#endif

#if defined(USE_APIC) && !defined(CONFIG_SMP)
    if (config_no_apic) {
        DEBUGBASIC(("APIC disabled, using legacy PIC\n"));
    } else if (!apic_single_cpu_init()) {
        DEBUGBASIC(("APIC not present, using legacy PIC\n"));
    }
    DBGRSDT("post-apic");
#endif

    /* Reserve BIOS memory regions. */
    cut_memmap(&kinfo, BIOS_MEM_BEGIN, BIOS_MEM_END);
    cut_memmap(&kinfo, BASE_MEM_TOP,   UPPER_MEM_END);
    DBGRSDT("post-arch_init");
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

#ifdef CONFIG_SMP_VERBOSE
    /* DBG: '%' = AP about to iretq into a non-IDLE proc. */
    if (cpuid != bsp_cpu_id && p->p_endpoint != IDLE) {
        __asm__ __volatile__(
            "mov $0x3F8, %%dx; mov $'%%', %%al; outb %%al, %%dx"
            : : : "rax", "rdx");
    }
#endif

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
    BOOT_VERBOSE(printf("RUC kts=%d\n", trap_style));

    p->p_seg.p_kern_trap_style = KTS_NONE;

    /*
     * Restore this process's user %fs base (the TLS thread pointer).  The
     * kernel never touches %fs base for its own use (it uses %gs/swapgs for
     * per-CPU state), so the value lives only in the CPU register and must be
     * reloaded for the process we are about to resume.  Captured on kernel
     * entry (see context_stop()).  No-op until userland actually uses TLS.
     */
    if (use_fsgsbase)
        write_fsbase(p->p_seg.p_fsbase);

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
        {
            static unsigned _ruc_int_dispatch = 0;
            _ruc_int_dispatch++;
            BOOT_VERBOSE(printf(
                "RUC#%u DISPATCH (kts=%d → restore_user_context_int)\n",
                _ruc_int_dispatch, trap_style));

            BOOT_VERBOSE({
                /* One-shot dump: only first 30 entries to avoid flooding. */
                static int dumped = 0;
                if (dumped < 30) {
                    u8_t *up = (u8_t *)(uintptr_t)p->p_reg.pc;
                    printf("ruc: -> restore_user_context_int, cr3=0x%lx "
                        "pcid=%u pc=0x%lx sp=0x%lx cs=0x%lx ss=0x%lx psw=0x%lx\n",
                        (unsigned long)p->p_seg.p_cr3,
                        (unsigned)p->p_seg.p_pcid,
                        (unsigned long)p->p_reg.pc,
                        (unsigned long)p->p_reg.sp,
                        (unsigned long)p->p_reg.cs,
                        (unsigned long)p->p_reg.ss,
                        (unsigned long)p->p_reg.psw);
                    printf("ruc: bytes @ pc: %02x %02x %02x %02x "
                        "%02x %02x %02x %02x\n",
                        up[0], up[1], up[2], up[3],
                        up[4], up[5], up[6], up[7]);
                    dumped++;
                }
            });
        }
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
