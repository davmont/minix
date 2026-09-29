#include "syslib.h"

#include <string.h>

int sys_kill(proc_ep, signr)
endpoint_t proc_ep;		/* which proc_ep has exited */
int signr;			/* signal number: 1 - 16 */
{
/* A proc_ep has to be signaled via PM.  Tell the kernel. */
  message m;
  memset(&m, 0, sizeof(m));
  m.m_sigcalls.endpt = proc_ep;
  m.m_sigcalls.sig = signr;
  return(_kernel_call(SYS_KILL, &m));
}

/*
 * Like sys_kill(), for a fault signal: the si_code and address the process's
 * SA_SIGINFO handler should see (SIGSEGV with SEGV_MAPERR and the bad
 * address, ...).
 */
int sys_kill_fault(endpoint_t proc_ep, int signr, int code, vir_bytes addr)
{
  message m;

  memset(&m, 0, sizeof(m));
  m.m_sigcalls.endpt = proc_ep;
  m.m_sigcalls.sig = signr;
  m.m_sigcalls.fault_code = code;
  m.m_sigcalls.fault_addr = (void *) addr;
  return(_kernel_call(SYS_KILL, &m));
}
