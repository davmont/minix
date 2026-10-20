#ifndef _VM_GLO_H
#define _VM_GLO_H

#include <minix/sys_config.h>
#include <minix/type.h>
#include <minix/param.h>
#include <sys/stat.h>

#include "vm.h"
#include "vmproc.h"

#if _MAIN
#undef EXTERN
#define EXTERN
#endif

#define VMP_EXECTMP	_NR_PROCS
#define VMP_NR		_NR_PROCS+1

EXTERN struct vmproc vmproc[VMP_NR];

/* Monotonic growth clock: bumped whenever any process gains a resident page.
 * Recorded per-process (vm_oom_grow) so the OOM killer can favour a hog that
 * is actively growing over an equally large but quiescent process. */
EXTERN u32_t oom_grow_clock;

long enable_filemap;

typedef kinfo_t ixfer_kinfo_t;
EXTERN ixfer_kinfo_t kernel_boot_info;

#if SANITYCHECKS
EXTERN int nocheck;
EXTERN int incheck;
EXTERN int sc_lastline;
EXTERN const char *sc_lastfile;
#endif

extern struct minix_kerninfo *_minix_kerninfo;

/* mem types */
EXTERN  mem_type_t mem_type_anon,       /* anonymous memory */
        mem_type_directphys,		/* direct physical mapping memory */
	mem_type_anon_contig,		/* physically contig anon memory */
	mem_type_cache,			/* disk cache */
	mem_type_mappedfile,		/* memory with file contents */
	mem_type_shared;		/* memory shared by multiple processes */

/* total number of memory pages */
EXTERN int total_pages;
EXTERN int num_vm_instances;

/* Set while VM allocates memory of its own on behalf of a user process: the
 * page tables of its address space and the per-page arrays of its regions.
 * Those are that process's memory as much as its pages are, and are
 * allocated as such (PAF_USERMEM) so that they stop at the OOM reserve.
 * Save and restore it around use: such allocations nest. */
EXTERN int vm_usermem;

#endif /* !_VM_GLO_H */
