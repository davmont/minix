#ifndef _PAGETABLE_H
#define _PAGETABLE_H 1

/* Page-table entry type: 64-bit on amd64, 32-bit on i386. */
typedef u64_t pte_t;

#include <stdint.h>
#include <machine/vm.h>

#include "vm.h"

/*
 * x86-64 pagetable definitions for the VM server.
 * Maps generic ARCH_VM_* names to AMD64 paging constants.
 *
 * The VM server currently uses a two-level page-table abstraction
 * (pt_dir[] + pt_pt[][]). On x86-64 we expose the PD (level 2) as the
 * "page directory" and the PT (level 1) as the "page table", giving the
 * same 2 MB / 4 KB granularity as i386 large/small pages.
 */

/* Mapping flags */
#define PTF_WRITE	AMD64_VM_WRITE
#define PTF_READ	AMD64_VM_PRESENT	/* no separate read bit on x86-64 */
#define PTF_PRESENT	AMD64_VM_PRESENT
#define PTF_USER	AMD64_VM_USER
#define PTF_GLOBAL	AMD64_VM_GLOBAL
#define PTF_NOCACHE	(AMD64_VM_PWT | AMD64_VM_PCD)

#define ARCH_VM_DIR_ENTRIES	AMD64_VM_DIR_ENTRIES
#define ARCH_BIG_PAGE_SIZE	AMD64_BIG_PAGE_SIZE
#define ARCH_VM_ADDR_MASK	AMD64_VM_ADDR_MASK
#define ARCH_VM_PAGE_PRESENT	AMD64_VM_PRESENT
#define ARCH_VM_PDE_MASK	AMD64_VM_ADDR_MASK
#define ARCH_VM_PDE_PRESENT	AMD64_VM_PRESENT
#define ARCH_VM_PTE_PRESENT	AMD64_VM_PRESENT
#define ARCH_VM_PTE_USER	AMD64_VM_USER
#define ARCH_VM_PTE_RW		AMD64_VM_WRITE
#define ARCH_PAGEDIR_SIZE	AMD64_PAGE_SIZE
#define ARCH_VM_BIGPAGE		AMD64_VM_BIGPAGE
#define ARCH_VM_PT_ENTRIES	AMD64_VM_PT_ENTRIES

/* For arch-specific PT routines to check that no bits outside regular
 * flags are set. */
#define PTF_ALLFLAGS	(PTF_READ|PTF_WRITE|PTF_PRESENT|PTF_USER|PTF_GLOBAL|PTF_NOCACHE)

#define PFERR_NOPAGE(e)	(!((e) & AMD64_VM_PFE_P))
#define PFERR_PROT(e)	(((e) & AMD64_VM_PFE_P))
#define PFERR_WRITE(e)	((e) & AMD64_VM_PFE_W)
#define PFERR_READ(e)	(!((e) & AMD64_VM_PFE_W))

#define VM_PAGE_SIZE	AMD64_PAGE_SIZE

/* Virtual address -> PD index and PT index. */
#define ARCH_VM_PDE(v)	AMD64_VM_PD(v)
#define ARCH_VM_PTE(v)	AMD64_VM_PT(v)

#endif /* _PAGETABLE_H */
