/*
 * protect.c - amd64 protected-mode / long-mode descriptor table setup.
 *
 * Differences from i386:
 *
 *  GDT: 64-bit code segments need the L bit set and D/B cleared.
 *       Segment bases/limits are ignored in 64-bit mode (except for FS/GS).
 *       TSS descriptors are 16 bytes (two consecutive GDT slots).
 *
 *  IDT: Gate entries are 16 bytes; the handler offset is 64 bits, split
 *       across [15:0], [31:16], and [63:32] fields.
 *
 *  TSS: Used only for IST stack pointers (RSP0 for syscalls, IST1 for
 *       double faults, etc.).  Hardware task switching is gone.
 *
 *  SYSCALL: We configure STAR, LSTAR, and FMASK to set up the fast
 *           system-call path.
 */

#include <assert.h>
#include <string.h>

#include <minix/cpufeature.h>
#include <sys/types.h>
#include "kernel/kernel.h"
#include "include/arch_proto.h"
#include "include/archconst.h"

#include <sys/exec.h>
#include <libexec.h>

/* =========================================================================
 * Descriptor structures
 * ========================================================================= */

/* segdesc_s and desctableptr_s are defined in <machine/archtypes.h> */

/*
 * 16-byte system descriptor (used for TSS in 64-bit mode).
 * The upper 8 bytes extend the base address to 64 bits.
 */
struct sysdesc_s {
    u16_t limit_low;
    u16_t base_low;
    u8_t  base_middle;
    u8_t  access;
    u8_t  granularity;
    u8_t  base_high;
    u32_t base_upper;   /* bits 63:32 of base */
    u32_t reserved;
} __attribute__((packed));

/*
 * 16-byte 64-bit interrupt/trap gate descriptor.
 */
struct gatedesc64_s {
    u16_t offset_low;   /* handler RIP [15:0]  */
    u16_t selector;     /* code segment selector */
    u8_t  ist;          /* bits[2:0] = IST index (0 = legacy stack switch) */
    u8_t  type_attr;    /* P | DPL | type */
    u16_t offset_mid;   /* handler RIP [31:16] */
    u32_t offset_high;  /* handler RIP [63:32] */
    u32_t reserved;
} __attribute__((packed));

/* =========================================================================
 * Storage
 * ========================================================================= */

/*
 * GDT: null + kern_cs + kern_ds + user_cs_compat + user_ds + user_cs64 + per-cpu TSS pairs.
 * Each TSS takes two 8-byte slots (treated as one 16-byte sysdesc_s).
 */
static struct segdesc_s gdt[GDT_SIZE] __aligned(DESC_SIZE);

static struct gatedesc64_s idt[IDT_SIZE] __aligned(16);

struct tss_s tss[CONFIG_MAX_CPUS];

/* Per-CPU kernel stack top pointers (also stored in TSS.RSP0). */
u64_t k_percpu_stacks[CONFIG_MAX_CPUS];

/*
 * Per-CPU GS-base scratch area for the SYSCALL path.
 * syscall_entry does: swapgs; mov %rsp,%gs:0; mov %gs:8,%rsp
 * IA32_KERNEL_GS_BASE must point here so that GS:8 == tss[cpu].rsp0.
 */
struct percpu_gs_s {
    u64_t user_rsp;    /* offset 0: scratch for user RSP during SYSCALL */
    u64_t kernel_rsp;  /* offset 8: kernel RSP (== tss[cpu].rsp0) */
} percpu_gs[CONFIG_MAX_CPUS];

int prot_init_done = 0;

struct desctableptr_s gdt_desc, idt_desc;

/* =========================================================================
 * Helper: virtual -> physical address during early init
 * ========================================================================= */

phys_bytes vir2phys(void *vir)
{
    extern char _kern_vir_base, _kern_phys_base;
    u64_t offset = (vir_bytes)&_kern_vir_base - (vir_bytes)&_kern_phys_base;
    return (phys_bytes)(u64_t)vir - offset;
}

/* =========================================================================
 * 64-bit code segment descriptor
 * ========================================================================= */

static void init_codeseg64(int index, int privilege)
{
    struct segdesc_s *gp = &gdt[index];

    memset(gp, 0, sizeof(*gp));
    /*
     * Access byte: Present | DPL | type=code+readable
     * Granularity byte: L=1 (64-bit), D/B=0 (required when L=1)
     */
    gp->access      = (u8_t)(PRESENT | (privilege << DPL_SHIFT)
                             | SEGMENT | EXECUTABLE | READABLE);
    gp->granularity = DESC_LONG;    /* L bit */
}

/* =========================================================================
 * 64-bit data segment descriptor
 * ========================================================================= */

static void init_dataseg64(int index, int privilege)
{
    struct segdesc_s *gp = &gdt[index];

    memset(gp, 0, sizeof(*gp));
    gp->access      = (u8_t)(PRESENT | (privilege << DPL_SHIFT)
                             | SEGMENT | WRITEABLE | ACCESSED);
    gp->granularity = 0;
}

/* =========================================================================
 * 64-bit TSS descriptor (occupies two consecutive GDT slots)
 * ========================================================================= */

static void init_tss_desc(int index, struct tss_s *t)
{
    /*
     * Cast the first GDT slot as a 16-byte sysdesc_s.  The second slot is
     * implicitly consumed (contains base[63:32] and a reserved word).
     */
    struct sysdesc_s *sd = (struct sysdesc_s *)&gdt[index];
    u64_t base = (u64_t)t;
    u32_t limit = sizeof(*t) - 1;

    sd->limit_low   = (u16_t)(limit & 0xFFFF);
    sd->base_low    = (u16_t)(base & 0xFFFF);
    sd->base_middle = (u8_t)((base >> 16) & 0xFF);
    sd->access      = (u8_t)(PRESENT | (INTR_PRIVILEGE << DPL_SHIFT)
                             | DESC_TYPE_TSS64);
    sd->granularity = (u8_t)((limit >> 16) & 0x0F);
    sd->base_high   = (u8_t)((base >> 24) & 0xFF);
    sd->base_upper  = (u32_t)(base >> 32);
    sd->reserved    = 0;
}

/* =========================================================================
 * TSS initialisation
 * ========================================================================= */

int tss_init(unsigned cpu, void *kernel_stack)
{
    struct tss_s *t = &tss[cpu];
    int index = TSS_INDEX(cpu);

    init_tss_desc(index, t);

    memset(t, 0, sizeof(*t));

    /*
     * RSP0: the kernel stack pointer used when a SYSCALL or interrupt
     * transitions from ring 3 to ring 0.  We reserve space at the top for
     * the proc pointer and CPU id (matching i386 convention).
     */
    k_percpu_stacks[cpu] = t->rsp0 =
        ((u64_t)(uintptr_t)kernel_stack) - X86_STACK_TOP_RESERVED;

    /* Store CPU id just above the reserved area. */
    *((reg_t *)(uintptr_t)(t->rsp0 + sizeof(reg_t))) = (reg_t)cpu;

    /*
     * Initialize the GS-base scratch area for this cpu.  Its kernel_rsp
     * mirrors tss[cpu].rsp0 so that syscall_entry's
     *   swapgs; mov %gs:8, %rsp
     * loads the right per-cpu kernel stack.
     *
     * IA32_KERNEL_GS_BASE is a per-CPU MSR: writing it changes the value
     * for the CPU currently executing this function, not for `cpu'.  So
     * we must only program it when initializing the CPU we are running
     * on.  Initializing TSS data for *other* CPUs (e.g. APs that are not
     * yet brought up) must not clobber the BSP's MSR -- otherwise
     * syscall_entry on the BSP would later swapgs to another CPU's
     * percpu_gs and land on the wrong kernel stack.
     */
    percpu_gs[cpu].user_rsp   = 0;
    percpu_gs[cpu].kernel_rsp = t->rsp0;
    if (cpu == cpuid) {
        u64_t gs_base = (u64_t)(uintptr_t)&percpu_gs[cpu];
        ia32_msr_write(AMD_MSR_KERNEL_GS_BASE,
                       (u32_t)(gs_base >> 32),
                       (u32_t)(gs_base & 0xFFFFFFFFU));
    }

    /*
     * IST1: dedicated double-fault stack so a stack overflow cannot
     * prevent the double-fault handler from running.
     */
    static u8_t df_stacks[CONFIG_MAX_CPUS][IST_STACKSIZE]
        __attribute__((aligned(16)));
    t->ist[IST_DF] = (u64_t)(uintptr_t)(df_stacks[cpu] + IST_STACKSIZE);

    /*
     * IST2: dedicated NMI stack.
     */
    static u8_t nmi_stacks[CONFIG_MAX_CPUS][IST_STACKSIZE]
        __attribute__((aligned(16)));
    t->ist[IST_NMI] = (u64_t)(uintptr_t)(nmi_stacks[cpu] + IST_STACKSIZE);

    t->iobase = (u16_t)sizeof(struct tss_s);    /* empty I/O map */

    /* ---- SYSCALL / SYSRET MSR configuration --------------------------------
     *
     * STAR[47:32]  = kernel CS selector          (SYSRET: CS = STAR[47:32]+16,
     *                                             SS = STAR[47:32]+8)
     * STAR[63:48]  = user CS selector base        (SYSCALL: CS = STAR[63:48],
     *                                             SS = STAR[63:48]+8)
     * LSTAR        = 64-bit SYSCALL entry RIP
     * FMASK        = bits to clear in RFLAGS on SYSCALL (clear IF to disable
     *                interrupts on entry; kernel re-enables selectively)
     */
    {
        u32_t star_lo, star_hi;
        u32_t efer_lo, efer_hi;

        /* Enable SCE (SYSCALL Enable) in EFER. */
        ia32_msr_read(AMD_MSR_EFER, &efer_hi, &efer_lo);
        efer_lo |= AMD_EFER_SCE;
        ia32_msr_write(AMD_MSR_EFER, efer_hi, efer_lo);

        /*
         * STAR: high 32 bits contain selector pair.
         * Bits[47:32] = KERN_CS_SELECTOR        -> SYSCALL CS.
         * Bits[63:48] = USER_CS_COMPAT_SELECTOR -> SYSRETQ arithmetic base:
         *               SYSRETQ CS = +16 = USER_CS_SELECTOR (GDT[5])
         *               SYSRETQ SS = +8  = USER_DS_SELECTOR (GDT[4])
         * Low 32 bits = entry point (ignored in 64-bit mode; use LSTAR).
         */
        star_lo = 0;
        star_hi = ((u32_t)USER_CS_COMPAT_SELECTOR << 16) | KERN_CS_SELECTOR;
        ia32_msr_write(AMD_MSR_STAR, star_hi, star_lo);

        /* LSTAR = syscall_entry handler address. */
        {
            u64_t lstar = (u64_t)(uintptr_t)syscall_entry;
            ia32_msr_write(AMD_MSR_LSTAR,
                           (u32_t)(lstar >> 32),
                           (u32_t)(lstar & 0xFFFFFFFF));
        }

        /* FMASK: clear IF (bit 9) on SYSCALL so interrupts are off. */
        ia32_msr_write(AMD_MSR_FMASK, 0, (u32_t)IF_MASK);

        /*
         * Tell userland that SYSCALL is wired up.  On amd64 SYSCALL is part of
         * the long-mode ABI and always available, so we set this unconditionally
         * after the MSRs above are configured.  Without this flag,
         * arch_phys_map_reply() exposes the slower softint stubs to libc.
         */
        minix_feature_flags |= MKF_I386_AMD_SYSCALL;
    }

    return (int)SEG_SELECTOR(index);
}

/* =========================================================================
 * 64-bit interrupt gate
 * ========================================================================= */

static void int_gate64(unsigned vec_nr, u64_t offset,
                       unsigned dpl_type, unsigned ist_slot)
{
    struct gatedesc64_s *gp = &idt[vec_nr];
    u8_t type_attr = (u8_t)(PRESENT | dpl_type);

    gp->offset_low  = (u16_t)(offset & 0xFFFF);
    gp->selector    = KERN_CS_SELECTOR;
    gp->ist         = (u8_t)(ist_slot & 0x07);
    gp->type_attr   = type_attr;
    gp->offset_mid  = (u16_t)((offset >> 16) & 0xFFFF);
    gp->offset_high = (u32_t)(offset >> 32);
    gp->reserved    = 0;
}

void int_gate_idt(unsigned vec_nr, vir_bytes offset, unsigned dpl_type)
{
    int_gate64(vec_nr, (u64_t)offset, dpl_type, 0 /* no IST */);
}

/* =========================================================================
 * Gate table helpers (same interface as i386 protect.c)
 * ========================================================================= */

static struct gate_table_s gate_table_pic[] = {
    { hwint00, VECTOR( 0), INTR_PRIVILEGE },
    { hwint01, VECTOR( 1), INTR_PRIVILEGE },
    { hwint02, VECTOR( 2), INTR_PRIVILEGE },
    { hwint03, VECTOR( 3), INTR_PRIVILEGE },
    { hwint04, VECTOR( 4), INTR_PRIVILEGE },
    { hwint05, VECTOR( 5), INTR_PRIVILEGE },
    { hwint06, VECTOR( 6), INTR_PRIVILEGE },
    { hwint07, VECTOR( 7), INTR_PRIVILEGE },
    { hwint08, VECTOR( 8), INTR_PRIVILEGE },
    { hwint09, VECTOR( 9), INTR_PRIVILEGE },
    { hwint10, VECTOR(10), INTR_PRIVILEGE },
    { hwint11, VECTOR(11), INTR_PRIVILEGE },
    { hwint12, VECTOR(12), INTR_PRIVILEGE },
    { hwint13, VECTOR(13), INTR_PRIVILEGE },
    { hwint14, VECTOR(14), INTR_PRIVILEGE },
    { hwint15, VECTOR(15), INTR_PRIVILEGE },
    { NULL, 0, 0}
};

static struct gate_table_s gate_table_exceptions[] = {
    { divide_error,          DIVIDE_VECTOR,         INTR_PRIVILEGE },
    { single_step_exception, DEBUG_VECTOR,           INTR_PRIVILEGE },
    { nmi,                   NMI_VECTOR,             INTR_PRIVILEGE },
    { breakpoint_exception,  BREAKPOINT_VECTOR,      USER_PRIVILEGE  },
    { overflow,              OVERFLOW_VECTOR,        USER_PRIVILEGE  },
    { bounds_check,          BOUNDS_VECTOR,          INTR_PRIVILEGE },
    { inval_opcode,          INVAL_OP_VECTOR,        INTR_PRIVILEGE },
    { copr_not_available,    COPROC_NOT_VECTOR,      INTR_PRIVILEGE },
    { double_fault,          DOUBLE_FAULT_VECTOR,    INTR_PRIVILEGE },
    { copr_seg_overrun,      COPROC_SEG_VECTOR,      INTR_PRIVILEGE },
    { inval_tss,             INVAL_TSS_VECTOR,       INTR_PRIVILEGE },
    { segment_not_present,   SEG_NOT_VECTOR,         INTR_PRIVILEGE },
    { stack_exception,       STACK_FAULT_VECTOR,     INTR_PRIVILEGE },
    { general_protection,    PROTECTION_VECTOR,      INTR_PRIVILEGE },
    { page_fault,            PAGE_FAULT_VECTOR,      INTR_PRIVILEGE },
    { copr_error,            COPROC_ERR_VECTOR,      INTR_PRIVILEGE },
    { alignment_check,       ALIGNMENT_CHECK_VECTOR, INTR_PRIVILEGE },
    { machine_check,         MACHINE_CHECK_VECTOR,   INTR_PRIVILEGE },
    { simd_exception,        SIMD_EXCEPTION_VECTOR,  INTR_PRIVILEGE },
    { ipc_entry_softint_orig, IPC_VECTOR_ORIG,       USER_PRIVILEGE  },
    { kernel_call_entry_orig, KERN_CALL_VECTOR_ORIG, USER_PRIVILEGE  },
    { ipc_entry_softint_um,   IPC_VECTOR_UM,         USER_PRIVILEGE  },
    { kernel_call_entry_um,   KERN_CALL_VECTOR_UM,   USER_PRIVILEGE  },
    { NULL, 0, 0}
};

void idt_copy_vectors(struct gate_table_s *first)
{
    struct gate_table_s *gtp;
    unsigned ist;

    for (gtp = first; gtp->gate; gtp++) {
        /*
         * Use IST slots for double fault and NMI to guarantee a clean
         * stack even when the interrupt fires with a corrupted RSP.
         */
        if (gtp->vec_nr == DOUBLE_FAULT_VECTOR)
            ist = IST_DF;
        else if (gtp->vec_nr == NMI_VECTOR)
            ist = IST_NMI;
        else
            ist = 0;

        int_gate64(gtp->vec_nr,
                   (u64_t)(uintptr_t)gtp->gate,
                   (unsigned)(PRESENT | DESC_TYPE_INT64 |
                              ((unsigned)gtp->privilege << DPL_SHIFT)),
                   ist);
    }
}

void idt_copy_vectors_pic(void)
{
    idt_copy_vectors(gate_table_pic);
}

void idt_init(void)
{
    idt_copy_vectors_pic();
    idt_copy_vectors(gate_table_exceptions);
}

void idt_reload(void)
{
    x86_lidt(&idt_desc);
}

/*
 * ap_set_kernel_gs_base — program IA32_KERNEL_GS_BASE for the CURRENT CPU.
 *
 * tss_init only programs the MSR when `cpu == cpuid` (the BSP path),
 * because the MSR is per-CPU and must be written FROM the target CPU.
 * APs call this from ap_finish_booting after we know their cpuid.
 */
void ap_set_kernel_gs_base(unsigned cpu)
{
    u64_t gs_base = (u64_t)(uintptr_t)&percpu_gs[cpu];
    ia32_msr_write(AMD_MSR_KERNEL_GS_BASE,
                   (u32_t)(gs_base >> 32),
                   (u32_t)(gs_base & 0xFFFFFFFFU));
}

/*
 * ap_setup_syscall_msrs — program EFER.SCE, STAR, LSTAR and FMASK on the
 * CURRENT CPU.  These are per-CPU MSRs that BSP programs once for itself
 * inside tss_init; APs must call this from their own context so the
 * SYSCALL instruction in userland doesn't #UD.  Mirrors the SYSCALL/SYSRET
 * block in tss_init().
 */
void ap_setup_syscall_msrs(void)
{
    u32_t efer_hi, efer_lo;
    u32_t star_lo, star_hi;
    u64_t lstar;

    ia32_msr_read(AMD_MSR_EFER, &efer_hi, &efer_lo);
    efer_lo |= AMD_EFER_SCE;
    ia32_msr_write(AMD_MSR_EFER, efer_hi, efer_lo);

    star_lo = 0;
    star_hi = ((u32_t)USER_CS_COMPAT_SELECTOR << 16) | KERN_CS_SELECTOR;
    ia32_msr_write(AMD_MSR_STAR, star_hi, star_lo);

    lstar = (u64_t)(uintptr_t)syscall_entry;
    ia32_msr_write(AMD_MSR_LSTAR,
                   (u32_t)(lstar >> 32),
                   (u32_t)(lstar & 0xFFFFFFFF));

    ia32_msr_write(AMD_MSR_FMASK, 0, (u32_t)IF_MASK);
}

/* =========================================================================
 * prot_load_selectors — load GDT, IDT, LDT, TSS and segment registers
 * ========================================================================= */

int booting_cpu = 0;

void prot_load_selectors(void)
{
    x86_lgdt(&gdt_desc);
    idt_init();
    idt_reload();
    /* No LDT in 64-bit mode. */
    x86_ltr(TSS_SELECTOR(booting_cpu));

    x86_load_kerncs();
    x86_load_ds(KERN_DS_SELECTOR);
    x86_load_es(KERN_DS_SELECTOR);
    x86_load_fs(0);     /* FS/GS bases set via MSR when needed */
    x86_load_gs(0);
    x86_load_ss(KERN_DS_SELECTOR);
}

/* =========================================================================
 * prot_init — called once during kernel startup
 * ========================================================================= */

void prot_init(void)
{
    extern char k_boot_stktop;

    memset(gdt, 0, sizeof(gdt));
    memset(idt, 0, sizeof(idt));

    /* GDT pointer. */
    gdt_desc.limit = (u16_t)(sizeof(gdt) - 1);
    gdt_desc.base  = (u64_t)(uintptr_t)gdt;

    /* IDT pointer. */
    idt_desc.limit = (u16_t)(sizeof(idt) - 1);
    idt_desc.base  = (u64_t)(uintptr_t)idt;

    /* Build GDT entries. */
    init_codeseg64(KERN_CS_INDEX, INTR_PRIVILEGE);
    init_dataseg64(KERN_DS_INDEX, INTR_PRIVILEGE);
    init_codeseg64(USER_CS_COMPAT_INDEX, USER_PRIVILEGE);
    init_dataseg64(USER_DS_INDEX, USER_PRIVILEGE);
    init_codeseg64(USER_CS_INDEX, USER_PRIVILEGE);

    tss_init(0, &k_boot_stktop);

    prot_load_selectors();

    /*
     * Rebuild the bootstrap page table using the real page-mapping code,
     * then switch to it.  (pg_* functions are defined in pg_utils.c.)
     */
    pg_clear();
    pg_identity(&kinfo);
    pg_mapkernel();
    pg_load();

    prot_init_done = 1;
}

/* =========================================================================
 * Misc helpers
 * ========================================================================= */

void enable_iop(struct proc *pp)
{
    /* Allow the process to execute I/O instructions by setting IOPL=3. */
    pp->p_reg.psw |= IOPL_MASK;
}

static int alloc_for_vm = 0;

void arch_post_init(void)
{
    struct proc *vm;
    vm = proc_addr(VM_PROC_NR);
    get_cpulocal_var(ptproc) = vm;
    pg_info(&vm->p_seg.p_cr3, &vm->p_seg.p_cr3_v);
}

/*
 * Minimal libexec callbacks compiled with -mcmodel=kernel.
 * exec_general.c is not compiled for the kernel because its mmap/IPC
 * helpers pull in _minix_ipcvecs -> libminc.a(init.o) relocation issues.
 */
int libexec_copy_memcpy(struct exec_info *execi,
    off_t off, vir_bytes vaddr, size_t len)
{
    assert(off + (off_t)len <= (off_t)execi->hdr_len);
    memcpy((char *)vaddr, (char *)execi->hdr + off, len);
    return OK;
}

int libexec_clear_memset(struct exec_info *execi, vir_bytes vaddr, size_t len)
{
    memset((char *)vaddr, 0, len);
    return OK;
}

static int libexec_pg_alloc(struct exec_info *execi, vir_bytes vaddr, size_t len)
{
    BOOT_VERBOSE(printf("PGAL: vaddr=0x%lx len=%zu (will pg_map+memset)\n",
        (unsigned long)vaddr, len));
    pg_map(PG_ALLOCATEME, vaddr, vaddr + len, &kinfo);
    pg_load();
    memset((char *)vaddr, 0, len);
    alloc_for_vm += (int)len;
    return OK;
}

multiboot_module_t *bootmod(int pnr)
{
    int i;

    assert(pnr >= 0);

    for (i = NR_TASKS; i < NR_BOOT_PROCS; i++) {
        int p;
        p = i - NR_TASKS;
        if (image[i].proc_nr == pnr) {
            assert(p < MULTIBOOT_MAX_MODS);
            assert(p < kinfo.mbi.mi_mods_count);
            return &kinfo.module_list[p];
        }
    }

    panic("boot module %d not found", pnr);
}

void arch_boot_proc(struct boot_image *ip, struct proc *rp)
{
    multiboot_module_t *mod;
    struct ps_strings *psp;
    char *sp;

    if (rp->p_nr < 0) return;

    mod = bootmod(rp->p_nr);

    if (rp->p_nr == VM_PROC_NR) {
        struct exec_info execi;
        memset(&execi, 0, sizeof(execi));

        execi.stack_high = kinfo.user_sp;
        execi.stack_size = 64 * 1024;
        execi.proc_e     = ip->endpoint;
        execi.hdr        = (char *)(uintptr_t)mod->mod_start;
        execi.filesize   = execi.hdr_len = mod->mod_end - mod->mod_start;
        strlcpy(execi.progname, ip->proc_name, sizeof(execi.progname));
        execi.frame_len  = 0;

        execi.copymem                  = libexec_copy_memcpy;
        execi.clearmem                 = libexec_clear_memset;
        execi.allocmem_prealloc_junk   = libexec_pg_alloc;
        execi.allocmem_prealloc_cleared = libexec_pg_alloc;
        execi.allocmem_ondemand        = libexec_pg_alloc;
        execi.clearproc                = NULL;

        /* Copy the ELF binary to high physical memory (second gigabyte).
         * libexec_pg_alloc calls pg_map(PG_ALLOCATEME,...) which replaces
         * 2MB identity-map PD entries for the VM's virtual address range.
         * This destroys the identity mapping of the original module pages
         * (physical ~0x7FC000) after the first segment is allocated, making
         * subsequent reads through execi.hdr return zeros.  Placing the copy
         * at the top of physical RAM (allocated via pg_alloc_page) puts it in
         * PDPT[1] (1-2 GB) which none of the low-vaddr allocations touch. */
#ifndef AMD64_PAGE_SIZE
#define AMD64_PAGE_SIZE 4096UL
#endif
        {
            /* Use alloc_lowest() to get a CONTIGUOUS phys range.  The
             * previous code (loop calling pg_alloc_page n_pages times +
             * memcpy from "lowest base") was buggy because pg_alloc_page
             * doesn't guarantee allocations come from the same chunk —
             * when the highest-index chunk runs dry it falls back to
             * another chunk, and the resulting `base = lowest of N
             * scattered pages` doesn't actually own the contiguous N×4KB
             * range that memcpy then writes.  In practice that monolithic
             * memcpy clobbered the ACPI tables at phys 0x3ffe2335 and
             * broke the userspace ACPI service. */
            extern phys_bytes alloc_lowest(kinfo_t *cbi, phys_bytes len);
            size_t mod_len  = execi.hdr_len;
            phys_bytes base;
            base = alloc_lowest(&kinfo, mod_len);
            BOOT_VERBOSE(printf(
                "VMCOPY: base=0x%lx mod_len=%zu (contig phys [0x%lx, 0x%lx))\n",
                (unsigned long)base, mod_len,
                (unsigned long)base,
                (unsigned long)(base + roundup(mod_len, AMD64_PAGE_SIZE))));
            memcpy((void *)(uintptr_t)base, execi.hdr, mod_len);
            execi.hdr     = (char *)(uintptr_t)base;
            execi.filesize = execi.hdr_len = mod_len;
        }

        if (libexec_load_elf(&execi) != OK)
            panic("VM loading failed");

        sp  = (char *)execi.stack_high;
        sp -= sizeof(struct ps_strings);
        psp = (struct ps_strings *)sp;
        sp -= (sizeof(void *) + sizeof(void *) + sizeof(int));

        psp->ps_argvstr  = (char **)(sp + sizeof(int));
        psp->ps_nargvstr = 0;
        psp->ps_envstr   = psp->ps_argvstr + sizeof(void *);
        psp->ps_nenvstr  = 0;

        arch_proc_init(rp, execi.pc, (vir_bytes)sp,
                       execi.stack_high - sizeof(struct ps_strings),
                       ip->proc_name);

        add_memmap(&kinfo, mod->mod_start, mod->mod_end - mod->mod_start);
        mod->mod_end = mod->mod_start = 0;

        kinfo.vm_allocated_bytes = alloc_for_vm;
    }
}
