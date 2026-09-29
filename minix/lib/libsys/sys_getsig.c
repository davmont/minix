#include "syslib.h"

/* The fault the kernel reported with the last sys_getksig(). */
static int last_fault_sig, last_fault_code;
static vir_bytes last_fault_addr;

/*===========================================================================*
 *                                sys_getksig				     *
 *===========================================================================*/
int sys_getksig(proc_ep, k_sig_map)
endpoint_t *proc_ep;			/* return process number here */
sigset_t *k_sig_map;			/* return signal map here */
{
    message m;
    int result;

    result = _kernel_call(SYS_GETKSIG, &m);
    *proc_ep = m.m_sigcalls.endpt;
    *k_sig_map = m.m_sigcalls.map;
    last_fault_sig = m.m_sigcalls.fault_sig;
    last_fault_code = m.m_sigcalls.fault_code;
    last_fault_addr = (vir_bytes) m.m_sigcalls.fault_addr;
    return(result);
}

/*===========================================================================*
 *                             sys_getksig_fault			     *
 *===========================================================================*/
int sys_getksig_fault(int *signo, int *code, vir_bytes *addr)
{
/* The fault behind the signals of the last sys_getksig(): its signal number,
 * si_code and address.  Returns 0 (and signo 0) if there was none.
 */
    *signo = last_fault_sig;
    *code = last_fault_code;
    *addr = last_fault_addr;
    return last_fault_sig != 0;
}

