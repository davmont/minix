
#ifndef _VMPROC_H 
#define _VMPROC_H 1

#include <minix/bitmap.h>
#include <machine/archtypes.h>

#include "pt.h"
#include "vm.h"
#include "regionavl.h"

struct vmproc;

struct vmproc {
	int		vm_flags;
	endpoint_t	vm_endpoint;
	pt_t		vm_pt;	/* page table data */
	struct boot_image *vm_boot; /* if boot time process */

	/* Regions in virtual address space. */
	region_avl vm_regions_avl;
	vir_bytes  vm_region_top;	/* highest vaddr last inserted */
	int vm_acl;
	int vm_slot;		/* process table slot */
#if VMSTATS
	int vm_bytecopies;
#endif
	vir_bytes	vm_total;
	vir_bytes	vm_total_max;
	u32_t		vm_oom_grow;	/* growth clock at last resident growth
					 * (OOM killer prefers active hogs) */
	u64_t		vm_as_limit;	/* RLIMIT_AS soft limit, bytes (0=none) */
	u64_t		vm_data_limit;	/* RLIMIT_DATA soft limit, bytes (0=none) */
	u64_t		vm_minor_page_fault;
	u64_t		vm_major_page_fault;

	/* Thread (LWP) support: a thread shares its group leader's page tables
	 * (CR3) but has its own (empty) region tree.  vm_lwp_leader is the slot
	 * of the leader whose region tree owns the shared address space, so the
	 * thread's page faults can be resolved against it.  NO_LWP_LEADER for a
	 * normal process. */
	int		vm_lwp_leader;

	/* On a thread-group LEADER: number of live members sharing this address
	 * space (the leader itself plus its threads).  0 on a non-leader / plain
	 * process.  The shared page tables are freed only on the 1->0 transition
	 * (the last member's exit) — see free_proc(). */
	int		vm_lwp_refcount;
};

/* Sentinel for vm_lwp_leader: this vmproc is not a thread. */
#define NO_LWP_LEADER	(-1)

/* Bits for vm_flags */
#define VMF_INUSE	0x001	/* slot contains a process */
#define VMF_EXITING	0x002	/* PM is cleaning up this process */
#define VMF_VM_INSTANCE 0x010   /* This is a VM process instance */

#endif
