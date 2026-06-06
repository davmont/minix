/* The kernel call implemented in this file:
 *   m_type:	SYS_EXEC
 *
 * The parameters for this kernel call are:
 *   m_lsys_krn_sys_exec.endpt		(process that did exec call)
 *   m_lsys_krn_sys_exec.stack		(new stack pointer)
 *   m_lsys_krn_sys_exec.name		(pointer to program name)
 *   m_lsys_krn_sys_exec.ip		(new instruction pointer)
 *   m_lsys_krn_sys_exec.ps_str		(struct ps_strings *)
 */
#include "kernel/system.h"
#include <string.h>
#include <minix/endpoint.h>

#if USE_EXEC

/*===========================================================================*
 *				do_exec					     *
 *===========================================================================*/
int do_exec(struct proc * caller, message * m_ptr)
{
/* Handle sys_exec().  A process has done a successful EXEC. Patch it up. */
  register struct proc *rp;
  int proc_nr;
  char name[PROC_NAME_LEN];

  if(!isokendpt(m_ptr->m_lsys_krn_sys_exec.endpt, &proc_nr))
	return EINVAL;

  rp = proc_addr(proc_nr);

  if(rp->p_misc_flags & MF_DELIVERMSG) {
	rp->p_misc_flags &= ~MF_DELIVERMSG;
  }

  /* Save command name for debugging, ps(1) output, etc. */
  if(data_copy(caller->p_endpoint, m_ptr->m_lsys_krn_sys_exec.name,
	KERNEL, (vir_bytes) name,
	(phys_bytes) sizeof(name) - 1) != OK)
  	strncpy(name, "<unset>", PROC_NAME_LEN);

  name[sizeof(name)-1] = '\0';

  /* Set process state. */
  arch_proc_init(rp,
	(vir_bytes) m_ptr->m_lsys_krn_sys_exec.ip,
	(vir_bytes) m_ptr->m_lsys_krn_sys_exec.stack,
	(vir_bytes) m_ptr->m_lsys_krn_sys_exec.ps_str, name);

#if defined(__x86_64__)
  /* Install an initial TLS (%fs) base if one was supplied — used to give a
   * newly created thread its own TLS.  arch_proc_reset() (called from
   * arch_proc_init) cleared p_fsbase to 0; a normal exec passes 0 here and lets
   * ld.elf_so install the TCB itself via WRFSBASE. */
  if (m_ptr->m_lsys_krn_sys_exec.tlsbase != 0)
	rp->p_seg.p_fsbase = (reg_t) m_ptr->m_lsys_krn_sys_exec.tlsbase;
#endif

  /* No reply to EXEC call */
  RTS_UNSET(rp, RTS_RECEIVING);

  /* Mark fpu_regs contents as not significant, so fpu
   * will be initialized, when it's used next time. */
  rp->p_misc_flags &= ~MF_FPU_INITIALIZED;
  /* force reloading FPU if the current process is the owner */
  release_fpu(rp);
  return(OK);
}
#endif /* USE_EXEC */
