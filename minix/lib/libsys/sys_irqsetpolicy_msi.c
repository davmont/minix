#include "syslib.h"

/*===========================================================================*
 *                          sys_irqsetpolicy_msi			     *
 *===========================================================================*/
/* Allocate an MSI/MSI-X vector from the kernel and install an interrupt policy
 * for it.  On success *hook_id (a notify id on input) receives the kernel hook
 * index, and *msi_addr / *msi_data receive the message address/data pair the
 * caller must program into the device's MSI-X table (or MSI capability) to
 * raise the interrupt.  Delivery is then identical to a legacy IRQ line.
 */
int sys_irqsetpolicy_msi(int policy, int *hook_id, u32_t *msi_addr,
	u32_t *msi_data)
{
    message m_irq;
    int s;

    m_irq.m_type = SYS_IRQCTL;
    m_irq.m_lsys_krn_sys_irqctl.request = IRQ_SETPOLICY_MSI;
    m_irq.m_lsys_krn_sys_irqctl.vector = 0;	/* kernel allocates the vector */
    m_irq.m_lsys_krn_sys_irqctl.policy = policy;
    m_irq.m_lsys_krn_sys_irqctl.hook_id = *hook_id;

    s = _kernel_call(SYS_IRQCTL, &m_irq);
    if (s == OK) {
	*hook_id = m_irq.m_krn_lsys_sys_irqctl.hook_id;
	if (msi_addr != NULL) *msi_addr = m_irq.m_krn_lsys_sys_irqctl.msi_addr;
	if (msi_data != NULL) *msi_data = m_irq.m_krn_lsys_sys_irqctl.msi_data;
    }
    return(s);
}
