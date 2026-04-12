/*
 * EHCI core driver interface.
 *
 * Exposes the two entry points called by usbd_amd64.c to bring the EHCI
 * host controller up and down.  Everything else is internal to ehci_core.c.
 */

#ifndef _EHCI_CORE_H_
#define _EHCI_CORE_H_


/*
 * Initialize the EHCI host controller found at *dev, register the IRQ,
 * fill in the hcd_driver_state operations table, and start the controller.
 * Returns EXIT_SUCCESS on success.
 */
int ehci_init(void);

/*
 * Shut down the EHCI host controller, release IRQ and MMIO mappings.
 */
void ehci_deinit(void);


#endif /* !_EHCI_CORE_H_ */
