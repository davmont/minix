/*
 * OHCI PCI discovery and MMIO mapping.
 *
 * Walks the PCI bus looking for a USB 1.1 OHCI host controller
 * (class 0x0C / subclass 0x03 / progif 0x10), then maps its MMIO BAR
 * into the driver's address space so the OHCI core can access registers.
 *
 * The structure mirrors ehci_pci.c on purpose: each HCD owns its own
 * PCI helper, so platform-init code can keep calling them independently.
 */

#include <stdlib.h>

#include <minix/drivers.h>	/* errno */
#include <minix/com.h>

#include <machine/pci.h>	/* PCI attribute constants */

#include <usbd/hcd_common.h>
#include <usbd/usbd_common.h>

#include "ohci_pci.h"
#include "ohci_regs.h"


/*===========================================================================*
 *    ohci_pci_find                                                          *
 *===========================================================================*/
int
ohci_pci_find(ohci_pci_device *dev)
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

		if (base_class == OHCI_PCI_CLASS  &&
		    sub_class  == OHCI_PCI_SUBCLASS &&
		    prog_if    == OHCI_PCI_PROGIF) {
			USB_MSG("Found OHCI controller: vid=0x%04X did=0x%04X",
				vid, did);
			dev->devind    = devind;
			dev->base_addr = pci_attr_r32(devind, OHCI_PCI_BAR0)
					 & OHCI_PCI_BAR0_MMIO_MASK;
			dev->mmio_size = OHCI_MMIO_SIZE;
			dev->irq       = pci_attr_r8(devind, OHCI_PCI_IRQ);
			return EXIT_SUCCESS;
		}
	} while (pci_next_dev(&devind, &vid, &did));

	USB_MSG("No OHCI controller found on PCI bus");
	return EXIT_FAILURE;
}


/*===========================================================================*
 *    ohci_pci_map                                                           *
 *===========================================================================*/
int
ohci_pci_map(ohci_pci_device *dev)
{
	DEBUG_DUMP;

	if (dev->base_addr == 0) {
		USB_MSG("OHCI BAR0 is zero, cannot map MMIO");
		return EXIT_FAILURE;
	}

	dev->op_regs = hcd_os_regs_init(dev->base_addr, dev->mmio_size);
	if (dev->op_regs == NULL) {
		USB_MSG("Failed to map OHCI MMIO at 0x%lx", dev->base_addr);
		return EXIT_FAILURE;
	}

	USB_MSG("OHCI MMIO mapped: op_regs=%p (size=0x%lx)",
		dev->op_regs, dev->mmio_size);

	return EXIT_SUCCESS;
}


/*===========================================================================*
 *    ohci_pci_unmap                                                         *
 *===========================================================================*/
void
ohci_pci_unmap(ohci_pci_device *dev)
{
	DEBUG_DUMP;

	if (dev->op_regs != NULL) {
		if (hcd_os_regs_deinit(dev->base_addr, dev->mmio_size) != 0)
			USB_MSG("Warning: failed to unmap OHCI MMIO");
		dev->op_regs = NULL;
	}
}
