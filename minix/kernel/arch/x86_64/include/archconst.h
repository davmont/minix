
#ifndef _AMD64_ACONST_H
#define _AMD64_ACONST_H 1

#include <machine/interrupt.h>
#include <machine/memory.h>

/* Table sizes. */
#define IDT_SIZE 256

/*
 * GDT layout for amd64.
 * In 64-bit mode segment limits/bases are mostly ignored, but the descriptor
 * format still matters for DPL, present bit and the L (64-bit) flag.
 * TSS descriptors occupy TWO consecutive GDT slots in 64-bit mode.
 */
#define KERN_CS_INDEX        1
#define KERN_DS_INDEX        2
#define USER_CS_INDEX        3
#define USER_DS_INDEX        4
#define TSS_INDEX_FIRST      5
/* Each TSS occupies 2 GDT slots in long mode (16-byte system descriptor) */
#define TSS_INDEX(cpu)       (TSS_INDEX_FIRST + 2 * (cpu))
#define GDT_SIZE             (TSS_INDEX(CONFIG_MAX_CPUS))

#define SEG_SELECTOR(i)          ((i) * 8)
#define KERN_CS_SELECTOR         SEG_SELECTOR(KERN_CS_INDEX)
#define KERN_DS_SELECTOR         SEG_SELECTOR(KERN_DS_INDEX)
#define USER_CS_SELECTOR         (SEG_SELECTOR(USER_CS_INDEX) | USER_PRIVILEGE)
#define USER_DS_SELECTOR         (SEG_SELECTOR(USER_DS_INDEX) | USER_PRIVILEGE)
#define TSS_SELECTOR(cpu)        SEG_SELECTOR(TSS_INDEX(cpu))

#define DESC_SIZE   8  /* standard descriptor size */

/* Privilege levels. */
#define INTR_PRIVILEGE  0
#define USER_PRIVILEGE  3
#define RPL_MASK        0x03

/* Segment descriptor access byte bits (legacy fields, still present). */
#define PRESENT       0x80
#define DPL           0x60
#define DPL_SHIFT        5
#define SEGMENT       0x10
#define EXECUTABLE    0x08
#define READABLE      0x02
#define WRITEABLE     0x02
#define ACCESSED      0x01

/* amd64 descriptor type fields */
#define DESC_TYPE_TSS64   0x09  /* 64-bit available TSS */
#define DESC_TYPE_INT64   0x0E  /* 64-bit interrupt gate */
#define DESC_TYPE_TRAP64  0x0F  /* 64-bit trap gate */

/* INT_GATE_TYPE: combined PRESENT + 64-bit interrupt gate (same bit value as i386) */
#define INT_GATE_TYPE     (PRESENT | DESC_TYPE_INT64)

/* Granularity / attribute byte for 64-bit code/data segments */
#define DESC_LONG         0x20  /* L bit: 64-bit code segment */
#define DESC_DB           0x40  /* D/B bit (must be 0 for 64-bit code segs) */
#define DESC_GRAN         0x80  /* G bit: page granularity */

/* Exception vector numbers (same as i386). */
#define DIVIDE_VECTOR        0
#define DEBUG_VECTOR         1
#define NMI_VECTOR           2
#define BREAKPOINT_VECTOR    3
#define OVERFLOW_VECTOR      4
#define BOUNDS_VECTOR        5
#define INVAL_OP_VECTOR      6
#define COPROC_NOT_VECTOR    7
#define DOUBLE_FAULT_VECTOR  8
#define COPROC_SEG_VECTOR    9
#define INVAL_TSS_VECTOR    10
#define SEG_NOT_VECTOR      11
#define STACK_FAULT_VECTOR  12
#define PROTECTION_VECTOR   13
#define PAGE_FAULT_VECTOR   14
#define COPROC_ERR_VECTOR   16
#define ALIGNMENT_CHECK_VECTOR 17
#define MACHINE_CHECK_VECTOR   18
#define SIMD_EXCEPTION_VECTOR  19

/* EFER MSR bits */
#define AMD_MSR_EFER        0xC0000080
#define AMD_EFER_SCE        (1 << 0)   /* SYSCALL/SYSRET enable */
#define AMD_EFER_LME        (1 << 8)   /* Long Mode Enable */
#define AMD_EFER_LMA        (1 << 10)  /* Long Mode Active (read-only) */
#define AMD_EFER_NXE        (1 << 11)  /* No-Execute Enable */

/* SYSCALL/SYSRET MSRs */
#define AMD_MSR_STAR        0xC0000081  /* SYSCALL CS/SS selectors */
#define AMD_MSR_LSTAR       0xC0000082  /* SYSCALL 64-bit entry RIP */
#define AMD_MSR_FMASK       0xC0000084  /* SYSCALL RFLAGS mask */

/* CR4 bits needed for Long Mode */
#define CR4_PAE             (1 << 5)   /* Physical Address Extension */
#define CR4_PGE             (1 << 7)   /* Page Global Enable */
#define CR4_OSFXSR          (1 << 9)   /* SSE enable (FXSAVE/FXRSTOR) */
#define CR4_OSXMMEXCPT      (1 << 10)  /* SSE #XM exception enable */
#define CR4_FSGSBASE        (1 << 16)  /* Enable RDFSBASE/WRFSBASE etc. */
#define CR4_PCIDE           (1 << 17)  /* Process-Context ID Enable */
#define CR4_OSXSAVE         (1 << 18)  /* XSAVE/XRSTOR and AVX enable */

/* CR0 bits */
#define CR0_PE              (1 << 0)   /* Protection Enable */
#define CR0_MP              (1 << 1)   /* Monitor Coprocessor */
#define CR0_EM              (1 << 2)   /* Emulate FPU */
#define CR0_WP              (1 << 16)  /* Write Protect */
#define CR0_PG              (1 << 31)  /* Paging Enable */

/* IST (Interrupt Stack Table) entries in the 64-bit TSS */
#define IST_STACKSIZE   4096
#define IST_DF          1   /* Double fault uses IST slot 1 */
#define IST_NMI         2   /* NMI uses IST slot 2 */

/* Stack reserved space at top (cpu id + proc ptr) */
#define X86_STACK_TOP_RESERVED  (2 * sizeof(reg_t))

/* Kernel trap style (how we entered the kernel) */
#define KTS_NONE        1
#define KTS_INT_HARD    2
#define KTS_INT_ORIG    3
#define KTS_INT_UM      4
#define KTS_FULLCONTEXT 5
#define KTS_SYSCALL     6

/* PSW / RFLAGS bits */
#define INIT_PSW        0x0200  /* IF set */
#define INIT_TASK_PSW   0x1200  /* initial psw for tasks (with IOPL 1) */
#define IF_MASK         0x0000000000000200ULL
#define IOPL_MASK       0x0000000000003000ULL
#define TRACEBIT        0x0000000000000100ULL
#define SETPSW(rp, new)         /* permits only certain bits to be set */ \
        ((rp)->p_reg.psw = ((rp)->p_reg.psw & ~0xCD5) | ((new) & 0xCD5))

/* User-accessible RFLAGS bits */
#define X86_FLAGS_USER  (0x8D5ULL)  /* CF|PF|AF|ZF|SF|TF|DF|OF|NT */

/* CPU vendor detection (CPUID leaf 0) */
#define INTEL_CPUID_GEN_EBX     0x756e6547 /* "Genu" */
#define INTEL_CPUID_GEN_EDX     0x49656e69 /* "ineI" */
#define INTEL_CPUID_GEN_ECX     0x6c65746e /* "ntel" */
#define AMD_CPUID_GEN_EBX       0x68747541 /* "Auth" */
#define AMD_CPUID_GEN_EDX       0x69746e65 /* "enti" */
#define AMD_CPUID_GEN_ECX       0x444d4163 /* "cAMD" */
#define CPU_VENDOR_INTEL        0
#define CPU_VENDOR_AMD          2
#define CPU_VENDOR_UNKNOWN      0xff

/* FPU context alignment — defined in <machine/fpu.h> (FPUALIGN 64 for XSAVE) */

/* Allocate-on-demand sentinel for pg_map() */
#define PG_ALLOCATEME   ((phys_bytes)-1)

/* Intel architectural performance counter MSRs (used by arch_watchdog.c) */
#define INTEL_MSR_PERFMON_CRT0          0xC1
#define INTEL_MSR_PERFMON_SEL0          0x186
#define INTEL_MSR_PERFMON_SEL0_ENABLE   (1 << 22)

#endif /* _AMD64_ACONST_H */
