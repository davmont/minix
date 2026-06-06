
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
#include <minix/debug.h>
#include <minix/bitmap.h>

#include <string.h>
#include <errno.h>
#include <assert.h>

#include "glo.h"
#include "vm.h"
#include "proto.h"
#include "util.h"
#include "sanitycheck.h"
#include "region.h"

/*===========================================================================*
 *				do_fork					     *
 *===========================================================================*/
int do_fork(message *msg)
{
  int r, proc, childproc;
  struct vmproc *vmp, *vmc;
  pt_t origpt;
  vir_bytes msgaddr;

  SANITYCHECK(SCL_FUNCTIONS);

  if(vm_isokendpt(msg->VMF_ENDPOINT, &proc) != OK) {
	printf("VM: bogus endpoint VM_FORK %d\n", msg->VMF_ENDPOINT);
  SANITYCHECK(SCL_FUNCTIONS);
	return EINVAL;
  }

  childproc = msg->VMF_SLOTNO;
  if(childproc < 0 || childproc >= NR_PROCS) {
	printf("VM: bogus slotno VM_FORK %d\n", msg->VMF_SLOTNO);
  SANITYCHECK(SCL_FUNCTIONS);
	return EINVAL;
  }

  vmp = &vmproc[proc];		/* parent */
  vmc = &vmproc[childproc];	/* child */
  assert(vmc->vm_slot == childproc);

  /* The child is basically a copy of the parent. */
  origpt = vmc->vm_pt;
  *vmc = *vmp;
  vmc->vm_slot = childproc;
  region_init(&vmc->vm_regions_avl);
  vmc->vm_endpoint = NONE;	/* In case someone tries to use it. */
  vmc->vm_pt = origpt;
  vmc->vm_lwp_leader = NO_LWP_LEADER;	/* not a thread unless set below */

#if VMSTATS
  vmc->vm_bytecopies = 0;
#endif

  /*
   * Thread (LWP) fork: the child SHARES the parent's address space — the same
   * page-table root (CR3) — instead of getting a private copy.  Skip pt_new()
   * and map_proc_copy(); the kernel keeps the parent's CR3 in the child (the
   * struct copy in kernel do_fork inherits it on amd64) because we do NOT
   * request PFF_VMINHIBIT and do NOT pt_bind() a new pagetable.
   *
   * NOTE: the child's own vir_region tree stays empty for now; the thread runs
   * on already-mapped memory (its stack is preallocated by the caller).
   * Servicing the thread's own page faults against the parent's regions is the
   * Phase-2 follow-up (see the pthread port plan).
   */
  if(msg->VMF_FORKFLAGS & VMFF_LWP) {
	vmc->vm_pt = vmp->vm_pt;	/* share parent's page tables */
	vmc->vm_flags &= VMF_INUSE;
	/* Record the thread-group leader: the vmproc whose region tree owns the
	 * shared address space.  If the creator is itself a thread, inherit its
	 * leader; otherwise the creator is the leader. */
	vmc->vm_lwp_leader =
	    (vmp->vm_lwp_leader != NO_LWP_LEADER) ? vmp->vm_lwp_leader : proc;
	acl_fork(vmc);
	if((r=sys_fork(vmp->vm_endpoint, childproc,
		&vmc->vm_endpoint, 0 /* no VMINHIBIT */, &msgaddr)) != OK) {
		return r;
	}
	msg->VMF_CHILD_ENDPOINT = vmc->vm_endpoint;
	SANITYCHECK(SCL_FUNCTIONS);
	return OK;
  }

  if(pt_new(&vmc->vm_pt) != OK) {
	return ENOMEM;
  }

  SANITYCHECK(SCL_DETAIL);

  if(map_proc_copy(vmc, vmp) != OK) {
	printf("VM: fork: map_proc_copy failed\n");
	pt_free(&vmc->vm_pt);
	return(ENOMEM);
  }

  /* Only inherit these flags. */
  vmc->vm_flags &= VMF_INUSE;

  /* Deal with ACLs. */
  acl_fork(vmc);

  /* Tell kernel about the (now successful) FORK. */
  if((r=sys_fork(vmp->vm_endpoint, childproc,
	&vmc->vm_endpoint, PFF_VMINHIBIT, &msgaddr)) != OK) {
        panic("do_fork can't sys_fork: %d", r);
  }

  if((r=pt_bind(&vmc->vm_pt, vmc)) != OK)
	panic("fork can't pt_bind: %d", r);

  {
	vir_bytes vir;
	/* making these messages writable is an optimisation
	 * and its return value needn't be checked.
	 */
	vir = msgaddr;
	if (handle_memory_once(vmc, vir, sizeof(message), 1) != OK)
	    panic("do_fork: handle_memory for child failed\n");
	vir = msgaddr;
	if (handle_memory_once(vmp, vir, sizeof(message), 1) != OK)
	    panic("do_fork: handle_memory for parent failed\n");
  }

  /* Inform caller of new child endpoint. */
  msg->VMF_CHILD_ENDPOINT = vmc->vm_endpoint;

  SANITYCHECK(SCL_FUNCTIONS);
  return OK;
}

