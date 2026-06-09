/*
 * xHCI PCI discovery and MMIO mapping.
 *
 * Walks the PCI bus for a USB 3.x xHCI host controller (class 0x0C /
 * subclass 0x03 / progif 0x30) and maps its MMIO BAR.  Mirrors ohci_pci.c /
 * ehci_pci.c so platform-init can call each HCD's helper independently.
 */

#include <stdlib.h>

#include <minix/drivers.h>
#include <minix/com.h>

#include <machine/pci.h>

#include <usbd/hcd_common.h>
#include <usbd/usbd_common.h>

#include "xhci_pci.h"
#include "xhci_regs.h"


/*===========================================================================*
 *    xhci_pci_find                                                          *
 *===========================================================================*/
int
xhci_pci_find(xhci_pci_device *dev)
{
	int devind;
	u16_t vid, did;
	u8_t base_class, sub_class, prog_if;

	DEBUG_DUMP;

	pci_init();

	if (!pci_first_dev(&devind, &vid, &did)) {
		USB_MSG("No PCI devices found");
		return EXIT_FAILURE;
	}

	do {
		base_class = pci_attr_r8(devind, PCI_BCR);
		sub_class  = pci_attr_r8(devind, PCI_SCR);
		prog_if    = pci_attr_r8(devind, PCI_PIFR);

		if (base_class == XHCI_PCI_CLASS  &&
		    sub_class  == XHCI_PCI_SUBCLASS &&
		    prog_if    == XHCI_PCI_PROGIF) {
			USB_MSG("Found xHCI controller: vid=0x%04X did=0x%04X",
				vid, did);
			dev->devind    = devind;
			dev->base_addr = pci_attr_r32(devind, XHCI_PCI_BAR0)
					 & XHCI_PCI_BAR0_MMIO_MASK;
			dev->mmio_size = XHCI_MMIO_SIZE;
			dev->irq       = pci_attr_r8(devind, XHCI_PCI_IRQ);
			pci_reserve(devind);
			return EXIT_SUCCESS;
		}
	} while (pci_next_dev(&devind, &vid, &did));

	USB_MSG("No xHCI controller found on PCI bus");
	return EXIT_FAILURE;
}


/*===========================================================================*
 *    xhci_pci_map                                                           *
 *===========================================================================*/
int
xhci_pci_map(xhci_pci_device *dev)
{
	DEBUG_DUMP;

	if (dev->base_addr == 0) {
		USB_MSG("xHCI BAR0 is zero, cannot map MMIO");
		return EXIT_FAILURE;
	}

	dev->regs = hcd_os_regs_init(dev->base_addr, dev->mmio_size);
	if (dev->regs == NULL) {
		USB_MSG("Failed to map xHCI MMIO at 0x%lx", dev->base_addr);
		return EXIT_FAILURE;
	}

	USB_MSG("xHCI MMIO mapped: regs=%p (size=0x%lx)",
		dev->regs, dev->mmio_size);

	return EXIT_SUCCESS;
}


/*===========================================================================*
 *    xhci_pci_unmap                                                         *
 *===========================================================================*/
void
xhci_pci_unmap(xhci_pci_device *dev)
{
	DEBUG_DUMP;

	if (dev->regs != NULL) {
		if (hcd_os_regs_deinit(dev->base_addr, dev->mmio_size) != 0)
			USB_MSG("Warning: failed to unmap xHCI MMIO");
		dev->regs = NULL;
	}
}
