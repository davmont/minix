#ifndef __SYS_VM_AMD64_H__
#define __SYS_VM_AMD64_H__

#define AMD64_PAGE_SIZE		4096

/* amd64 paging constants */
#define AMD64_VM_PRESENT	0x001	/* Page is present */
#define AMD64_VM_WRITE		0x002	/* Read/write access allowed */
#define AMD64_VM_READ		0x000	/* Read access only */
#define AMD64_VM_USER		0x004	/* User access allowed */
#define AMD64_VM_PWT		0x008	/* Write through */
#define AMD64_VM_PCD		0x010	/* Cache disable */
#define AMD64_VM_ACC		0x020	/* Accessed */
#define AMD64_VM_DIRTY		0x040	/* Dirty */
#define AMD64_VM_PS  		0x080	/* Page size (2MB/1GB) */
#define AMD64_VM_GLOBAL   	0x100	/* Global. */
#define AMD64_VM_ADDR_MASK 	0x000FFFFFFFFFF000ULL /* physical address mask */

/* 4-level paging entry sizes and shifts */
#define AMD64_VM_PT_ENT_SIZE	8	/* Size of a page table entry (64-bit) */
#define AMD64_VM_PT_ENT_SHIFT	12	/* Shift to get entry in page table */
#define AMD64_VM_PD_ENT_SHIFT	21
#define AMD64_VM_PDP_ENT_SHIFT	30
#define AMD64_VM_PML4_ENT_SHIFT	39

/* CR0 bits */
#define AMD64_CR0_PE		0x00000001
#define AMD64_CR0_PG		0x80000000

/* CR4 bits */
#define AMD64_CR4_PAE		0x00000020
#define AMD64_CR4_PGE		0x00000080

#ifndef __ASSEMBLY__
#include <minix/type.h>
struct vm_ep_data {
	struct mem_map	* mem_map;
	vir_bytes	data_seg_limit;
};
#endif
#endif
