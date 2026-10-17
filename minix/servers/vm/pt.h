
#ifndef _PT_H
#define _PT_H 1

#include <machine/vm.h>

#include "vm.h"
#include "pagetable.h"

/* A pagetable. */
#if defined(__x86_64__)
/* The levels above the page tables, as VM tracks them (pagetable.c).  A
 * page directory: its table, and the VM-virtual address of each of its 512
 * page tables (a page of pointers). */
struct pt_pd {
	pte_t		*pd;
	phys_bytes	 pd_phys;
	pte_t		**pts;
};
/* A page-directory-pointer table and its 512 page directories. */
struct pt_pdpt {
	u64_t		*pdpt;
	phys_bytes	 pdpt_phys;
	struct pt_pd	**pds;
};
/* The 256 user PML4 entries (one page). */
#define PT_ROOT_SLOTS	256
struct pt_root {
	struct pt_pdpt	*pdpts[PT_ROOT_SLOTS];
};
#endif

typedef struct {
#if defined(__x86_64__)
	/* Root of the four-level page-table hierarchy (CR3 points here). */
	u64_t		*pt_pml4;	/* PML4 virtual pointer (512 entries) */
	phys_bytes	 pt_pml4_phys;	/* PML4 physical address (= CR3 value) */

	/* PDPT 0 (PML4[0]) and PD 0 (its entry 0, 0-1 GB): always there; PD 0
	 * also holds the kernel's mappings and the page directory windows,
	 * above the user part (kern_start_pde in pagetable.c). */
	u64_t		*pt_pdpt;
	phys_bytes	 pt_pdpt_phys;
	pte_t		*pt_dir;
	phys_bytes	 pt_dir_phys;

	/* VM's view of the whole user half (PML4[0..255], 128 TB): where the
	 * PDPTs, PDs and PTs it made are in its own address space.  A thread
	 * group shares it, with the page tables (see pagetable.c). */
	struct pt_root	*pt_root;
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
