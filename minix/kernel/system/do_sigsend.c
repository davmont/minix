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
  /* Static, not on the 4 KB kernel stack: with the SA_SIGINFO siginfo_t
   * and ucontext_t the frame is 1.7 KB.  Kernel calls run under the BKL,
   * and the frame is rebuilt from scratch on every (re)try.  The BKL is
   * dropped once, inside save_fpu() (smp_schedule_sync() for a process
   * whose FPU state lives on another CPU); that is safe only because PM
   * is the sole SYS_SIGSEND caller and stays blocked in this call. */
  static struct sigframe_sigcontext fr;
  struct sigframe_sigcontext *frp;
  int proc_nr, r, onaltstack;
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

  /* Compute the user stack pointer where sigframe will start: the top of the
   * alternate signal stack for an SA_ONSTACK handler, unless the process is
   * already running on it (a signal in a signal handler). */
  smsg.sm_stkptr = arch_get_sp(rp);
  onaltstack = smsg.sm_altsize != 0 && smsg.sm_stkptr > smsg.sm_altbase &&
	smsg.sm_stkptr <= smsg.sm_altbase + smsg.sm_altsize;
  if ((smsg.sm_flags & SMF_ONSTACK) && smsg.sm_altsize != 0 && !onaltstack)
	smsg.sm_stkptr = smsg.sm_altbase + smsg.sm_altsize;
#if defined(__x86_64__)
  else {
	/* Staying on the interrupted stack: skip the 128-byte red zone below
	 * %rsp, which a leaf function may be using for its locals (the amd64
	 * ABI; userland is built with it).  Placing the frame right below
	 * %rsp overwrote them under an asynchronous signal. */
	smsg.sm_stkptr -= 128;
  }
#endif
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

#if defined(__x86_64__)
  if (smsg.sm_flags & SMF_SIGINFO) {
	/* SA_SIGINFO: what raised the signal, and the interrupted context. */
	fr.sf_si.si_signo = smsg.sm_signo;
	fr.sf_si.si_code = smsg.sm_code;
	fr.sf_si.si_pid = smsg.sm_pid;
	fr.sf_si.si_uid = smsg.sm_uid;
	if (smsg.sm_signo == SIGCHLD)
		fr.sf_si.si_status = smsg.sm_status;
	if (smsg.sm_addr != 0)
		fr.sf_si.si_addr = (void *) smsg.sm_addr;

	fr.sf_uc.uc_flags = _UC_SIGMASK | _UC_CPU | _UC_STACK;
	fr.sf_uc.uc_sigmask = smsg.sm_mask;
	/* The alternate signal stack as of the interrupted context. */
	if (smsg.sm_altsize != 0) {
		fr.sf_uc.uc_stack.ss_sp = (void *) smsg.sm_altbase;
		fr.sf_uc.uc_stack.ss_size = smsg.sm_altsize;
		if (onaltstack)
			fr.sf_uc.uc_stack.ss_flags = SS_ONSTACK;
	} else
		fr.sf_uc.uc_stack.ss_flags = SS_DISABLE;
	fr.sf_uc.uc_mcontext.__gregs[_REG_GS] = fr.sf_sc.sc_gs;
	fr.sf_uc.uc_mcontext.__gregs[_REG_FS] = fr.sf_sc.sc_fs;
	fr.sf_uc.uc_mcontext.__gregs[_REG_R15] = fr.sf_sc.sc_r15;
	fr.sf_uc.uc_mcontext.__gregs[_REG_R14] = fr.sf_sc.sc_r14;
	fr.sf_uc.uc_mcontext.__gregs[_REG_R13] = fr.sf_sc.sc_r13;
	fr.sf_uc.uc_mcontext.__gregs[_REG_R12] = fr.sf_sc.sc_r12;
	fr.sf_uc.uc_mcontext.__gregs[_REG_R11] = fr.sf_sc.sc_r11;
	fr.sf_uc.uc_mcontext.__gregs[_REG_R10] = fr.sf_sc.sc_r10;
	fr.sf_uc.uc_mcontext.__gregs[_REG_R9] = fr.sf_sc.sc_r9;
	fr.sf_uc.uc_mcontext.__gregs[_REG_R8] = fr.sf_sc.sc_r8;
	fr.sf_uc.uc_mcontext.__gregs[_REG_RBP] = fr.sf_sc.sc_rbp;
	fr.sf_uc.uc_mcontext.__gregs[_REG_RDI] = fr.sf_sc.sc_rdi;
	fr.sf_uc.uc_mcontext.__gregs[_REG_RSI] = fr.sf_sc.sc_rsi;
	fr.sf_uc.uc_mcontext.__gregs[_REG_RDX] = fr.sf_sc.sc_rdx;
	fr.sf_uc.uc_mcontext.__gregs[_REG_RCX] = fr.sf_sc.sc_rcx;
	fr.sf_uc.uc_mcontext.__gregs[_REG_RBX] = fr.sf_sc.sc_rbx;
	fr.sf_uc.uc_mcontext.__gregs[_REG_RAX] = fr.sf_sc.sc_rax;
	fr.sf_uc.uc_mcontext.__gregs[_REG_RIP] = fr.sf_sc.sc_rip;
	fr.sf_uc.uc_mcontext.__gregs[_REG_CS] = fr.sf_sc.sc_cs;
	fr.sf_uc.uc_mcontext.__gregs[_REG_RFLAGS] = fr.sf_sc.sc_rflags;
	fr.sf_uc.uc_mcontext.__gregs[_REG_RSP] = fr.sf_sc.sc_rsp;
	fr.sf_uc.uc_mcontext.__gregs[_REG_SS] = fr.sf_sc.sc_ss;
  }
#endif

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
  if (smsg.sm_flags & SMF_SIGINFO) {
	/* void handler(int signo, siginfo_t *info, void *ucontext) */
	rp->p_reg.rsi = (reg_t) &frp->sf_si;
	rp->p_reg.rdx = (reg_t) &frp->sf_uc;
  } else {
	rp->p_reg.rsi = (reg_t) fr.sf_code;
	rp->p_reg.rdx = (reg_t) fr.sf_scp;
  }
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

