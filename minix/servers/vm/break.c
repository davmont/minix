/* The MINIX model of memory allocation reserves a fixed amount of memory for
 * the combined text, data, and stack segments.  The amount used for a child
 * process created by FORK is the same as the parent had.  If the child does
 * an EXEC later, the new size is taken from the header of the file EXEC'ed.
 *
 * The layout in memory consists of the text segment, followed by the data
 * segment, followed by a gap (unused memory), followed by the stack segment.
 * The data segment grows upward and the stack grows downward, so each can
 * take memory from the gap.  If they meet, the process must be killed.  The
 * procedures in this file deal with the growth of the data and stack segments.
 *
 * The entry points into this file are:
 *   do_brk:      BRK/SBRK system calls to grow or shrink the data segment
 */

#define _SYSTEM 1

#include <minix/callnr.h>
#include <minix/com.h>
#include <minix/config.h>
#include <minix/const.h>
#include <minix/ds.h>
#include <minix/endpoint.h>
#include <minix/minlib.h>
#include <minix/type.h>
#include <minix/ipc.h>
#include <minix/sysutil.h>
#include <minix/syslib.h>
#include <minix/bitmap.h>

#include <errno.h>
#include <sys/resource.h>

#include "glo.h"
#include "vm.h"
#include "proto.h"
#include "util.h"

#define DATA_CHANGED       1    /* flag value when data segment size changed */
#define STACK_CHANGED      2    /* flag value when stack size changed */

/*===========================================================================*
 *				vm_rlimit_exceeded			     *
 *===========================================================================*/
/* Would growing the address space by 'add' bytes exceed the process's
 * RLIMIT_AS (always) or RLIMIT_DATA (when check_data) soft limit?  Both are
 * measured against total mapped address space; a zero limit means unlimited,
 * so processes that never call setrlimit() are unaffected. */
int vm_rlimit_exceeded(struct vmproc *vmp, vir_bytes add, int check_data)
{
	u64_t as;

	if(!vmp->vm_as_limit && !(check_data && vmp->vm_data_limit))
		return 0;			/* no applicable limit */

	as = (u64_t) vm_as_bytes(vmp) + add;
	if(vmp->vm_as_limit && as > vmp->vm_as_limit)
		return 1;
	if(check_data && vmp->vm_data_limit && as > vmp->vm_data_limit)
		return 1;
	return 0;
}

/*===========================================================================*
 *				do_rlimit				     *
 *===========================================================================*/
/* Get or set a process's RLIMIT_AS / RLIMIT_DATA soft limit (libc
 * setrlimit/getrlimit).  Stored per-process and enforced at mmap()/brk(). */
int do_rlimit(message *msg)
{
	int proc;
	struct vmproc *vmp;
	u64_t *slot;

	if (vm_isokendpt(msg->m_source, &proc) != OK)
		return EINVAL;
	vmp = &vmproc[proc];

	switch(msg->m_lc_vm_rlimit.which) {
	case RLIMIT_AS:		slot = &vmp->vm_as_limit;   break;
	case RLIMIT_DATA:	slot = &vmp->vm_data_limit; break;
	default:		return EINVAL;
	}

	if(msg->m_lc_vm_rlimit.op == VMRL_SET)
		*slot = msg->m_lc_vm_rlimit.limit;
	else
		msg->m_lc_vm_rlimit.limit = *slot;	/* VMRL_GET */
	return OK;
}

/*===========================================================================*
 *				do_brk					     *
 *===========================================================================*/
int do_brk(message *msg)
{
/* Perform the brk(addr) system call.
 * The parameter, 'addr' is the new virtual address in D space.
 */
	int proc;

	if (vm_isokendpt(msg->m_source, &proc) != OK) {
		printf("VM: bogus endpoint VM_BRK %d\n", msg->m_source);
		return EINVAL;
	}

	return real_brk(&vmproc[proc], (vir_bytes) msg->m_lc_vm_brk.addr);
}

/*===========================================================================*
 *				real_brk				     *
 *===========================================================================*/
int real_brk(struct vmproc *vmp, vir_bytes v)
{
	/* Enforce RLIMIT_AS / RLIMIT_DATA on data-segment growth: if the
	 * address space is already at the limit, refuse to grow it further so
	 * the process's own brk()/malloc() fails gracefully with ENOMEM rather
	 * than driving the system toward OOM. */
	if(vm_rlimit_exceeded(vmp, 0, 1 /*check_data*/))
		return(ENOMEM);

	if(map_region_extend_upto_v(vmp, v) == OK) {
		return OK;
	}

	return(ENOMEM);
}
