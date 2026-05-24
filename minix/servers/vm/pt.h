
#ifndef _PT_H
#define _PT_H 1

#include <machine/vm.h>

#include "vm.h"
#include "pagetable.h"

/* A pagetable. */
typedef struct {
#if defined(__x86_64__)
	/* Root of the four-level page-table hierarchy (CR3 points here). */
	u64_t		*pt_pml4;	/* PML4 virtual pointer (512 entries) */
	phys_bytes	 pt_pml4_phys;	/* PML4 physical address (= CR3 value) */

	/* User-space PDPT: PML4[0] → this table (512 entries). */
	u64_t		*pt_pdpt;
	phys_bytes	 pt_pdpt_phys;

	/* Page directory (PD, level 2): PDPT[0] → this table (512 entries).
	 * This is what the VM server calls the "page directory"; it covers
	 * 0–1 GB of virtual address space with 2 MB granularity.        */
	pte_t		*pt_dir;	/* page aligned (ARCH_VM_DIR_ENTRIES) */
	phys_bytes	 pt_dir_phys;	/* physical address of pt_dir */

	/* Page tables (PT, level 1): one per present PD entry. */
	pte_t		*pt_pt[ARCH_VM_DIR_ENTRIES];
#else
	/* i386 two-level page table. */
	u32_t		*pt_dir;	/* page aligned (ARCH_VM_DIR_ENTRIES) */
	u32_t		 pt_dir_phys;	/* physical address of pt_dir */
	u32_t		*pt_pt[ARCH_VM_DIR_ENTRIES];
#endif

	/* Hint for findhole(): start scanning here (linear address). */
	u32_t pt_virtop;
} pt_t;

#define CLICKSPERPAGE (VM_PAGE_SIZE/CLICK_SIZE)

#endif
