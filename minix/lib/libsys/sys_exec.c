#include "syslib.h"

int sys_exec(endpoint_t proc_ep, vir_bytes stack_ptr, vir_bytes progname,
	vir_bytes pc, vir_bytes ps_str, vir_bytes tlsbase, vir_bytes arg)
{
/* A process has exec'd (or a new thread is being set up).  Tell the kernel.
 * tlsbase, when nonzero, installs the initial %fs (TLS) base — used to give a
 * newly created thread its own TLS; normal exec passes 0 (ld.elf_so installs
 * the TCB itself).  arg, when nonzero, is placed in the new context's first
 * argument register (%rdi) — used to hand a new thread its trampoline cookie;
 * normal exec passes 0. */

	message m;

	m.m_lsys_krn_sys_exec.endpt = proc_ep;
	m.m_lsys_krn_sys_exec.stack = stack_ptr;
	m.m_lsys_krn_sys_exec.name = progname;
	m.m_lsys_krn_sys_exec.ip = pc;
	m.m_lsys_krn_sys_exec.ps_str = ps_str;
	m.m_lsys_krn_sys_exec.tlsbase = tlsbase;
	m.m_lsys_krn_sys_exec.arg = arg;

	return _kernel_call(SYS_EXEC, &m);
}
