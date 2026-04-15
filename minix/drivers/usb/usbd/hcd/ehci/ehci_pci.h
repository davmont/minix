/*
 * EHCI PCI discovery and initialization interface.
 *
 * Handles finding the EHCI controller via PCI enumeration, mapping its
 * MMIO registers, and registering its IRQ before handing off to the
 * generic EHCI core.
 */

#ifndef _EHCI_PCI_H_
#define _EHCI_PCI_H_

#include <usbd/hcd_common.h>


/*===========================================================================*
 *    EHCI device state (PCI-level)                                          *
 *===========================================================================*/
typedef struct ehci_pci_device {
	/* PCI device index as returned by pci_first_dev/pci_next_dev */
	int		devind;

	/* Base address of the EHCI MMIO region (physical) */
	hcd_addr	base_addr;

	/* Total MMIO size to map */
	unsigned long	mmio_size;

	/* IRQ line */
	int		irq;

	/* Pointer to mapped capability registers */
	void *		cap_regs;

	/* Pointer to mapped operational registers (cap_regs + CAPLENGTH) */
	void *		op_regs;
} ehci_pci_device;


/*===========================================================================*
 *    EHCI PCI interface                                                     *
 *===========================================================================*/

/*
 * Scan the PCI bus for an EHCI controller (class 0x0C, subclass 0x03,
 * progif 0x20).  Fills in dev->devind, dev->base_addr, dev->irq.
 * Returns EXIT_SUCCESS if a controller was found, EXIT_FAILURE otherwise.
 */
int ehci_pci_find(ehci_pci_device *dev);

/*
 * Map the EHCI MMIO region into the driver's address space and set up
 * dev->cap_regs and dev->op_regs.
 * Returns EXIT_SUCCESS on success.
 */
int ehci_pci_map(ehci_pci_device *dev);

/*
 * Unmap the EHCI MMIO region previously mapped by ehci_pci_map.
 */
void ehci_pci_unmap(ehci_pci_device *dev);


#endif /* !_EHCI_PCI_H_ */
