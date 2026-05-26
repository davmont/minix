/*
 * OHCI core driver interface.
 *
 * Exposes the two entry points called by usbd_amd64.c to bring the OHCI
 * host controller up and down.  Everything else is internal to ohci_core.c.
 */

#ifndef _OHCI_CORE_H_
#define _OHCI_CORE_H_


/*
 * Initialize the OHCI host controller: PCI discovery + MMIO mapping +
 * DMA pool allocation.  Operational bring-up (BIOS handoff, reset,
 * HCCA install, port reset, hcd_driver_state callbacks) lives in later
 * phases — this is Phase 3a scaffolding only.
 *
 * Returns EXIT_SUCCESS if a controller was found and mapped, EXIT_FAILURE
 * otherwise.  A return of EXIT_FAILURE is non-fatal at the usbd level:
 * usbd_init_hcd() also tries EHCI, so OHCI absence does not abort init.
 */
int ohci_init(void);

/*
 * Shut down the OHCI host controller: release MMIO mapping and DMA pools.
 */
void ohci_deinit(void);


#endif /* !_OHCI_CORE_H_ */
