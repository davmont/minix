/*
 * xHCI PCI discovery and initialization interface.
 *
 * Finds the xHCI controller via PCI enumeration and maps its single MMIO BAR
 * (which contains the capability, operational, runtime and doorbell windows)
 * before handing off to the xHCI core.
 */

#ifndef _XHCI_PCI_H_
#define _XHCI_PCI_H_

#include <usbd/hcd_common.h>


/*===========================================================================*
 *    xHCI device state (PCI-level)                                          *
 *===========================================================================*/
typedef struct xhci_pci_device {
	int		devind;		/* PCI device index */
	hcd_addr	base_addr;	/* MMIO BAR0 physical base */
	unsigned long	mmio_size;	/* mapped MMIO size */
	int		irq;		/* IRQ line */

	/* Mapped MMIO base (start of the capability registers).  The core
	 * derives the operational / runtime / doorbell windows from the
	 * capability registers (CAPLENGTH, RTSOFF, DBOFF). */
	void *		regs;
} xhci_pci_device;


/*===========================================================================*
 *    xHCI PCI interface                                                     *
 *===========================================================================*/
int  xhci_pci_find(xhci_pci_device *dev);
int  xhci_pci_map(xhci_pci_device *dev);
void xhci_pci_unmap(xhci_pci_device *dev);


#endif /* !_XHCI_PCI_H_ */
