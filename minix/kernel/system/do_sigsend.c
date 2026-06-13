/* The kernel call that is implemented in this file:
 *	m_type: SYS_SIGSEND
 *
 * The parameters for this kernel call are:
 * 	m_sigcalls.endpt	# process to call signal handler
 *	m_sigcalls.sigctx	# pointer to sigcontext structure
 *
 */

#include "kernel/system.h"
#include <signal.h>
#include <string.h>

#if USE_SIGSEND

/*===========================================================================*
 *			      do_sigsend				     *
 *===========================================================================*/
int do_sigsend(struct proc * caller, message * m_ptr)
{
/* Handle sys_sigsend, POSIX-style signal handling. */

  struct sigmsg smsg;
  register struct proc *rp;
  struct sigframe_sigcontext fr, *frp;
  int proc_nr, r;
#if defined(__i386__) || defined(__x86_64__)
  reg_t new_fp;
#endif

  if (!isokendpt(m_ptr->m_sigcalls.endpt, &proc_nr)) return EINVAL;
  if (iskerneln(proc_nr)) return EPERM;
  rp = proc_addr(proc_nr);

  /* Get the sigmsg structure into our address space.  */
  if ((r = data_copy_vmcheck(caller, caller->p_endpoint,
		(vir_bytes)m_ptr->m_sigcalls.sigctx, KERNEL,
		(vir_bytes)&smsg, (phys_bytes) sizeof(struct sigmsg))) != OK)
	return r;

  /* WARNING: the following code may be run more than once even for a single
   * signal delivery. Do not change registers here. See the comment below.
   */

  /* Compute the user stack pointer where sigframe will start. */
  smsg.sm_stkptr = arch_get_sp(rp);
  frp = (struct sigframe_sigcontext *) smsg.sm_stkptr - 1;

#if defined(__x86_64__)
  /* The amd64 System V ABI requires that, when control reaches a function,
   * (%rsp % 16) == 8 -- i.e. the stack is 16-byte aligned right after the
   * (implicit) return-address push.  restore_user_context() enters the handler
   * with %rsp == frp and sf_ra_sigreturn (at [frp]) acting as that pushed
   * return address, so frp itself must be congruent to 8 (mod 16): then the
   * handler's "push %rbp" lands %rbp on a 16-byte boundary and SSE stack
   * accesses (e.g. the movaps that zero a struct in libc's syscall/printf
   * stubs) do not raise a #GP.  Round the frame base down to (16k + 8). */
  frp = (struct sigframe_sigcontext *)
      ((((vir_bytes) frp - 8) & ~(vir_bytes) 0xF) + 8);
#endif

  /* Copy the registers to the sigcontext structure. */
  memset(&fr, 0, sizeof(fr));
  fr.sf_scp = &frp->sf_sc;

#if defined(__i386__)
  fr.sf_sc.sc_gs = rp->p_reg.gs;
  fr.sf_sc.sc_fs = rp->p_reg.fs;
  fr.sf_sc.sc_es = rp->p_reg.es;
  fr.sf_sc.sc_ds = rp->p_reg.ds;
  fr.sf_sc.sc_edi = rp->p_reg.di;
  fr.sf_sc.sc_esi = rp->p_reg.si;
  fr.sf_sc.sc_ebp = rp->p_reg.fp;
  fr.sf_sc.sc_ebx = rp->p_reg.bx;
  fr.sf_sc.sc_edx = rp->p_reg.dx;
  fr.sf_sc.sc_ecx = rp->p_reg.cx;
  fr.sf_sc.sc_eax = rp->p_reg.retreg;
  fr.sf_sc.sc_eip = rp->p_reg.pc;
  fr.sf_sc.sc_cs = rp->p_reg.cs;
  fr.sf_sc.sc_eflags = rp->p_reg.psw;
  fr.sf_sc.sc_esp = rp->p_reg.sp;
  fr.sf_sc.sc_ss = rp->p_reg.ss;
  fr.sf_fp = rp->p_reg.fp;
  fr.sf_signum = smsg.sm_signo;
  new_fp = (reg_t) &frp->sf_fp;
  fr.sf_scpcopy = fr.sf_scp;
  fr.sf_ra_sigreturn = smsg.sm_sigreturn;
  fr.sf_ra= rp->p_reg.pc;

  fr.sf_sc.trap_style = rp->p_seg.p_kern_trap_style;

  if (fr.sf_sc.trap_style == KTS_NONE) {
  	printf("do_sigsend: sigsend an unsaved process\n");
	return EINVAL;
  }

  if (proc_used_fpu(rp)) {
	/* save the FPU context before saving it to the sig context */
	save_fpu(rp);
	memcpy(&fr.sf_sc.sc_fpu_state, rp->p_seg.fpu_state, FPU_XFP_SIZE);
  }
#endif

#if defined(__x86_64__)
  fr.sf_sc.sc_gs = rp->p_reg.gs;
  fr.sf_sc.sc_fs = rp->p_reg.fs;
  fr.sf_sc.sc_rdi = rp->p_reg.rdi;
  fr.sf_sc.sc_rsi = rp->p_reg.rsi;
  fr.sf_sc.sc_rbp = rp->p_reg.rbp;
  fr.sf_sc.sc_rbx = rp->p_reg.rbx;
  fr.sf_sc.sc_rdx = rp->p_reg.rdx;
  fr.sf_sc.sc_rcx = rp->p_reg.rcx;
  fr.sf_sc.sc_rax = rp->p_reg.rax;
  fr.sf_sc.sc_r8  = rp->p_reg.r8;
  fr.sf_sc.sc_r9  = rp->p_reg.r9;
  fr.sf_sc.sc_r10 = rp->p_reg.r10;
  fr.sf_sc.sc_r11 = rp->p_reg.r11;
  fr.sf_sc.sc_r12 = rp->p_reg.r12;
  fr.sf_sc.sc_r13 = rp->p_reg.r13;
  fr.sf_sc.sc_r14 = rp->p_reg.r14;
  fr.sf_sc.sc_r15 = rp->p_reg.r15;
  fr.sf_sc.sc_rip = rp->p_reg.pc;
  fr.sf_sc.sc_cs = rp->p_reg.cs;
  fr.sf_sc.sc_rflags = rp->p_reg.psw;
  fr.sf_sc.sc_rsp = rp->p_reg.sp;
  fr.sf_sc.sc_ss = rp->p_reg.ss;
  fr.sf_fp = rp->p_reg.rbp;
  fr.sf_signum = smsg.sm_signo;
  new_fp = (reg_t) &frp->sf_fp;
  fr.sf_scpcopy = fr.sf_scp;
  fr.sf_ra_sigreturn = smsg.sm_sigreturn;
  fr.sf_ra = rp->p_reg.pc;

  fr.sf_sc.trap_style = rp->p_seg.p_kern_trap_style;

  if (fr.sf_sc.trap_style == KTS_NONE) {
	printf("do_sigsend: sigsend an unsaved process\n");
	return EINVAL;
  }

  if (proc_used_fpu(rp)) {
	/* save the FPU context before saving it to the sig context */
	save_fpu(rp);
	memcpy(&fr.sf_sc.sc_fpu_state, rp->p_seg.fpu_state, FPU_XFP_SIZE);
  }
#endif

#if defined(__arm__)
  fr.sf_sc.sc_spsr = rp->p_reg.psr;
  fr.sf_sc.sc_r0 = rp->p_reg.retreg;
  fr.sf_sc.sc_r1 = rp->p_reg.r1;
  fr.sf_sc.sc_r2 = rp->p_reg.r2;
  fr.sf_sc.sc_r3 = rp->p_reg.r3;
  fr.sf_sc.sc_r4 = rp->p_reg.r4;
  fr.sf_sc.sc_r5 = rp->p_reg.r5;
  fr.sf_sc.sc_r6 = rp->p_reg.r6;
  fr.sf_sc.sc_r7 = rp->p_reg.r7;
  fr.sf_sc.sc_r8 = rp->p_reg.r8;
  fr.sf_sc.sc_r9 = rp->p_reg.r9;
  fr.sf_sc.sc_r10 = rp->p_reg.r10;
  fr.sf_sc.sc_r11 = rp->p_reg.fp;
  fr.sf_sc.sc_r12 = rp->p_reg.r12;
  fr.sf_sc.sc_usr_sp = rp->p_reg.sp;
  fr.sf_sc.sc_usr_lr = rp->p_reg.lr;
  fr.sf_sc.sc_svc_lr = 0;	/* ? */
  fr.sf_sc.sc_pc = rp->p_reg.pc;	/* R15 */
#endif

  /* Finish the sigcontext initialization. */
  fr.sf_sc.sc_mask = smsg.sm_mask;
  fr.sf_sc.sc_flags = rp->p_misc_flags & MF_FPU_INITIALIZED;
  fr.sf_sc.sc_magic = SC_MAGIC;

  /* Initialize the sigframe structure. */
  fpu_sigcontext(rp, &fr, &fr.sf_sc);

  /* Copy the sigframe structure to the user's stack. */
  if ((r = data_copy_vmcheck(caller, KERNEL, (vir_bytes)&fr,
		m_ptr->m_sigcalls.endpt, (vir_bytes)frp,
		(vir_bytes)sizeof(struct sigframe_sigcontext))) != OK)
      return r;

  /* WARNING: up to the statement above, the code may run multiple times, since
   * copying out the frame/context may fail with VMSUSPEND the first time. For
   * that reason, changes to process registers *MUST* be deferred until after
   * this last copy -- otherwise, these changes will be made several times,
   * possibly leading to corrupted process state.
   */

  /* Reset user registers to execute the signal handler. */
  rp->p_reg.sp = (reg_t) frp;
  rp->p_reg.pc = (reg_t) smsg.sm_sighandler;

#if defined(__i386__)
  rp->p_reg.fp = new_fp;
#elif defined(__x86_64__)
  /* Link the frame pointer to the saved context and pass the handler its
   * arguments in registers, per the amd64 calling convention:
   * %rdi = signum, %rsi = code, %rdx = pointer to the sigcontext.
   * Also stash the sigcontext pointer in the callee-saved %r15: the handler
   * (and its callees) must preserve it per the ABI, so __sigreturn() can
   * recover it without depending on the exact stack offset of the frame. */
  rp->p_reg.rbp = new_fp;
  rp->p_reg.rdi = (reg_t) smsg.sm_signo;
  rp->p_reg.rsi = (reg_t) fr.sf_code;
  rp->p_reg.rdx = (reg_t) fr.sf_scp;
  rp->p_reg.r15 = (reg_t) fr.sf_scp;
#elif defined(__arm__)
  /* use the ARM link register to set the return address from the signal
   * handler
   */
  rp->p_reg.lr = (reg_t) smsg.sm_sigreturn;
  if(rp->p_reg.lr & 1) { printf("sigsend: LSB LR makes no sense.\n"); }

  /* pass signal handler parameters in registers */
  rp->p_reg.retreg = (reg_t) smsg.sm_signo;
  rp->p_reg.r1 = 0;	/* sf_code */
  rp->p_reg.r2 = (reg_t) fr.sf_scp;
  rp->p_misc_flags |= MF_CONTEXT_SET;
#endif

  /* Signal handler should get clean FPU. */
  rp->p_misc_flags &= ~MF_FPU_INITIALIZED;

  if(!RTS_ISSET(rp, RTS_PROC_STOP)) {
	printf("system: warning: sigsend a running process\n");
	printf("caller stack: ");
	proc_stacktrace(caller);
  }

  return OK;
}

#endif /* USE_SIGSEND */

