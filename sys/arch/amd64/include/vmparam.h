/*	$NetBSD: vmparam.h (adapted for amd64/x86_64)	*/

#ifndef _AMD64_VMPARAM_H_
#define _AMD64_VMPARAM_H_

/*
 * Machine dependent constants for x86_64 (LP64).
 */

#define	PAGE_SHIFT	12
#define	PAGE_SIZE	(1 << PAGE_SHIFT)
#define	PAGE_MASK	(PAGE_SIZE - 1)

/*
 * x86_64 canonical user address space: 0 to 2^47-1 (128 TB).
 */
#define	VM_MIN_ADDRESS		((vaddr_t)0)
#define	VM_MAXUSER_ADDRESS	((vaddr_t)0x00007ffffffff000UL)
#define	VM_MAX_ADDRESS		VM_MAXUSER_ADDRESS
#define	VM_MIN_KERNEL_ADDRESS	((vaddr_t)0xffff800000000000UL)
#define	VM_MAX_KERNEL_ADDRESS	((vaddr_t)0xffffffffffe00000UL)

#define	USRSTACK	VM_MAXUSER_ADDRESS

/*
 * Virtual memory related constants, all in bytes.
 */
#define	MAXTSIZ		(1UL*1024*1024*1024)	/* max text size (1G) */
#ifndef DFLDSIZ
#define	DFLDSIZ		(256*1024*1024)		/* initial data size limit */
#endif
#ifndef MAXDSIZ
#define	MAXDSIZ		(32UL*1024*1024*1024)	/* 32G max data size */
#endif
#ifndef MAXDSIZ_BU
#define	MAXDSIZ_BU	MAXDSIZ
#endif
#ifndef	DFLSSIZ
#define	DFLSSIZ		(8*1024*1024)		/* initial stack size limit */
#endif
#ifndef	MAXSSIZ
#define	MAXSSIZ		(128*1024*1024)		/* max stack size (128M) */
#endif

#define	USRIOSIZE	300

#define VM_PHYSSEG_MAX		32
#define VM_NFREELIST		4
#define VM_FREELIST_DEFAULT	0
#define VM_FREELIST_FIRST16	3
#define VM_FREELIST_FIRST1G	2
#define VM_FREELIST_FIRST4G	1

#define VM_PHYSSEG_STRAT	VM_PSTRAT_BIGFIRST
#define VM_PHYS_SIZE		(USRIOSIZE*PAGE_SIZE)

#define __USE_TOPDOWN_VM
#define VM_DEFAULT_ADDRESS_TOPDOWN(da, sz) \
	trunc_page(USRSTACK - MAXSSIZ - (sz))
#define VM_DEFAULT_ADDRESS_BOTTOMUP(da, sz) \
	round_page((vaddr_t)(da) + (vsize_t)MIN(maxdmap, MAXDSIZ_BU))

#ifndef VM_MAX_KERNEL_BUF
#define VM_MAX_KERNEL_BUF	(1024UL * 1024 * 1024)
#endif

#endif /* _AMD64_VMPARAM_H_ */
