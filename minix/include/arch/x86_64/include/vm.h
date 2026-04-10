#ifndef __SYS_VM_AMD64_H__
#define __SYS_VM_AMD64_H__
/*
 * amd64/vm.h - AMD64 / x86-64 paging constants.
 *
 * Four-level paging (PML4 → PDPT → PD → PT), 4 KB pages.
 * 2 MB pages are supported at the PD level (PS bit).
 * 1 GB pages are supported at the PDPT level (PS bit), if the CPU
 * advertises the 1GB page feature (CPUID leaf 0x80000001, EDX bit 26).
 */

#define AMD64_PAGE_SIZE		4096
#define AMD64_BIG_PAGE_SIZE	(2 * 1024 * 1024)		/* 2 MB */
#define AMD64_HUGE_PAGE_SIZE	(1024UL * 1024UL * 1024UL)	/* 1 GB */

/* Virtual-address decomposition shifts (9-bit fields at each level). */
#define AMD64_VM_PML4_SHIFT	39
#define AMD64_VM_PDPT_SHIFT	30
#define AMD64_VM_PD_SHIFT	21
#define AMD64_VM_PT_SHIFT	12

#define AMD64_VM_9BIT_MASK	0x1FFUL

/* Index extractors — evaluate to 0..511 */
#define AMD64_VM_PML4(v)  (((unsigned long)(v) >> AMD64_VM_PML4_SHIFT) & AMD64_VM_9BIT_MASK)
#define AMD64_VM_PDPT(v)  (((unsigned long)(v) >> AMD64_VM_PDPT_SHIFT) & AMD64_VM_9BIT_MASK)
#define AMD64_VM_PD(v)    (((unsigned long)(v) >> AMD64_VM_PD_SHIFT  ) & AMD64_VM_9BIT_MASK)
#define AMD64_VM_PT(v)    (((unsigned long)(v) >> AMD64_VM_PT_SHIFT  ) & AMD64_VM_9BIT_MASK)

/* Page-table / page-directory entry flags */
#define AMD64_VM_PRESENT	(1ULL <<  0)
#define AMD64_VM_WRITE		(1ULL <<  1)
#define AMD64_VM_USER		(1ULL <<  2)
#define AMD64_VM_PWT		(1ULL <<  3)	/* write-through */
#define AMD64_VM_PCD		(1ULL <<  4)	/* cache disable */
#define AMD64_VM_ACC		(1ULL <<  5)	/* accessed */
#define AMD64_VM_DIRTY		(1ULL <<  6)	/* dirty (PT only) */
#define AMD64_VM_PS		(1ULL <<  7)	/* page size: 2 MB in PD, 1 GB in PDPT */
#define AMD64_VM_BIGPAGE	AMD64_VM_PS
#define AMD64_VM_GLOBAL		(1ULL <<  8)
#define AMD64_VM_NX		(1ULL << 63)	/* no-execute (needs EFER.NXE) */

/* Physical-address masks */
#define AMD64_VM_ADDR_MASK	  0x000FFFFFFFFFF000ULL /* 4 KB aligned */
#define AMD64_VM_ADDR_MASK_2MB	  0x000FFFFFFFE00000ULL /* 2 MB aligned */
#define AMD64_VM_OFFSET_MASK	  0x0000000000000FFFULL /* 4 KB offset */
#define AMD64_VM_OFFSET_MASK_2MB  0x00000000001FFFFFULL /* 2 MB offset */

/* Extract page-frame address from a page-table entry */
#define AMD64_VM_PFA(e)		((e) & AMD64_VM_ADDR_MASK)

/* Number of entries per page-table level */
#define AMD64_VM_DIR_ENTRIES	512
#define AMD64_VM_PT_ENTRIES	512
#define AMD64_VM_PT_ENT_SIZE	8	/* bytes per entry */

/* Page-fault error code bits */
#define AMD64_VM_PFE_P	0x01	/* fault due to present-page protection */
#define AMD64_VM_PFE_W	0x02	/* caused by write */
#define AMD64_VM_PFE_U	0x04	/* CPU was in user mode */

/* CR0 bits */
#define AMD64_CR0_PE		0x00000001UL
#define AMD64_CR0_MP		0x00000002UL
#define AMD64_CR0_EM		0x00000004UL
#define AMD64_CR0_TS		0x00000008UL
#define AMD64_CR0_WP		0x00010000UL
#define AMD64_CR0_PG		0x80000000UL

/* CR4 bits */
#define AMD64_CR4_PSE		0x00000010UL	/* page-size extensions */
#define AMD64_CR4_PAE		0x00000020UL	/* physical-address extension */
#define AMD64_CR4_PGE		0x00000080UL	/* global-page enable */
#define AMD64_CR4_OSFXSR	0x00000200UL
#define AMD64_CR4_OSXMMEXCPT	0x00000400UL

/* CPUID flags */
#define CPUID1_EDX_FPU		(1UL)
#define CPUID1_EDX_PSE		(1UL <<  3)	/* Page Size Extension */
#define CPUID1_EDX_TSC		(1UL <<  4)	/* Timestamp counter */
#define CPUID1_EDX_PAE		(1UL <<  6)	/* Physical Address Extension */
#define CPUID1_EDX_APIC_ON_CHIP	(1UL <<  9)	/* APIC on chip */
#define CPUID1_EDX_SYSENTER	(1UL << 11)	/* Intel SYSENTER */
#define CPUID1_EDX_PGE		(1UL << 13)	/* Page Global Enable */
#define CPUID1_EDX_FXSR		(1UL << 24)
#define CPUID1_EDX_SSE		(1UL << 25)
#define CPUID1_EDX_SSE2		(1UL << 26)
#define CPUID1_EDX_HTT		(1UL << 28)
#define CPUID1_ECX_SSE3		(1UL)
#define CPUID1_ECX_SSSE3	(1UL <<  9)
#define CPUID1_ECX_SSE4_1	(1UL << 19)
#define CPUID1_ECX_SSE4_2	(1UL << 20)
#define CPUID_EF_EDX_SYSENTER	(1UL << 11)	/* Intel SYSENTER (extended) */

/*
 * I386_VM_* compatibility aliases for shared code (e.g. vm/pagetable.c)
 * that uses i386 names inside #if defined(__i386__) || defined(__x86_64__) blocks.
 * The bit positions are identical on x86_64.
 */
#define I386_VM_PRESENT		AMD64_VM_PRESENT
#define I386_VM_WRITE		AMD64_VM_WRITE
#define I386_VM_USER		AMD64_VM_USER
#define I386_VM_PWT		AMD64_VM_PWT
#define I386_VM_PCD		AMD64_VM_PCD
#define I386_VM_ACC		AMD64_VM_ACC
#define I386_VM_DIRTY		AMD64_VM_DIRTY
#define I386_VM_PS		AMD64_VM_PS
#define I386_VM_BIGPAGE		AMD64_VM_BIGPAGE
#define I386_VM_GLOBAL		AMD64_VM_GLOBAL
#define I386_VM_PTAVAIL1	(1ULL <<  9)
#define I386_VM_PTAVAIL2	(1ULL << 10)
#define I386_VM_PTAVAIL3	(1ULL << 11)
#define I386_VM_ADDR_MASK	AMD64_VM_ADDR_MASK
#define I386_VM_PFA(e)		AMD64_VM_PFA(e)
#define I386_VM_PT_ENT_SIZE	AMD64_VM_PT_ENT_SIZE
#define I386_VM_PT_ENTRIES	AMD64_VM_PT_ENTRIES
#define I386_VM_DIR_ENTRIES	AMD64_VM_DIR_ENTRIES
#define I386_VM_PFE_P		AMD64_VM_PFE_P
#define I386_VM_PFE_W		AMD64_VM_PFE_W
#define I386_VM_PFE_U		AMD64_VM_PFE_U

#ifndef __ASSEMBLY__
#include <minix/type.h>

/* Structure used by VM to pass data to the kernel during paging setup. */
struct vm_ep_data {
	struct mem_map	*mem_map;
	vir_bytes	 data_seg_limit;
};
#endif /* __ASSEMBLY__ */

#endif /* __SYS_VM_AMD64_H__ */
