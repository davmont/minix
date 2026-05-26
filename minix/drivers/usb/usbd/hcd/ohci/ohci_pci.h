/*
 * OHCI PCI discovery and initialization interface.
 *
 * Handles finding the OHCI controller via PCI enumeration and mapping its
 * MMIO registers before handing off to the generic OHCI core.
 */

#ifndef _OHCI_PCI_H_
#define _OHCI_PCI_H_

#include <usbd/hcd_common.h>


/*===========================================================================*
 *    OHCI device state (PCI-level)                                          *
 *===========================================================================*/
typedef struct ohci_pci_device {
	/* PCI device index as returned by pci_first_dev/pci_next_dev */
	int		devind;

	/* Base address of the OHCI MMIO region (physical) */
	hcd_addr	base_addr;

	/* Total MMIO size to map */
	unsigned long	mmio_size;

	/* IRQ line */
	int		irq;

	/* Pointer to mapped operational registers.  OHCI has no separate
	 * capability window, so this is the only register pointer needed. */
	void *		op_regs;
} ohci_pci_device;


/*===========================================================================*
 *    OHCI PCI interface                                                     *
 *===========================================================================*/

/*
 * Scan the PCI bus for an OHCI controller (class 0x0C, subclass 0x03,
 * progif 0x10).  Fills in dev->devind, dev->base_addr, dev->irq.
 * Returns EXIT_SUCCESS if a controller was found, EXIT_FAILURE otherwise.
 */
int ohci_pci_find(ohci_pci_device *dev);

/*
 * Map the OHCI MMIO region into the driver's address space and set up
 * dev->op_regs.  Returns EXIT_SUCCESS on success.
 */
int ohci_pci_map(ohci_pci_device *dev);

/*
 * Unmap the OHCI MMIO region previously mapped by ohci_pci_map.
 */
void ohci_pci_unmap(ohci_pci_device *dev);


#endif /* !_OHCI_PCI_H_ */
