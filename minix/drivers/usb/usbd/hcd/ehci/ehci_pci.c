/*
 * EHCI PCI discovery and MMIO mapping.
 *
 * Walks the PCI bus looking for a USB 2.0 EHCI host controller
 * (class 0x0C / subclass 0x03 / progif 0x20), then maps its MMIO BAR
 * into the driver's address space so the EHCI core can access registers.
 */

#include <stdlib.h>

#include <minix/drivers.h>	/* errno */
#include <minix/com.h>

#include <machine/pci.h>	/* PCI attribute constants */

#include <usbd/hcd_common.h>
#include <usbd/usbd_common.h>

#include "ehci_pci.h"
#include "ehci_regs.h"


/*===========================================================================*
 *    ehci_pci_find                                                          *
 *===========================================================================*/
int
ehci_pci_find(ehci_pci_device *dev)
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

		if (base_class == EHCI_PCI_CLASS  &&
		    sub_class  == EHCI_PCI_SUBCLASS &&
		    prog_if    == EHCI_PCI_PROGIF) {
			USB_MSG("Found EHCI controller: vid=0x%04X did=0x%04X",
				vid, did);
			dev->devind    = devind;
			dev->base_addr = pci_attr_r32(devind, EHCI_PCI_BAR0)
					 & EHCI_PCI_BAR0_MMIO_MASK;
			dev->mmio_size = EHCI_MMIO_SIZE;
			dev->irq       = pci_attr_r8(devind, EHCI_PCI_IRQ);
			/* Claim the device so the PCI server grants us its
			 * IRQ/resources (required before sys_irqsetpolicy). */
			pci_reserve(devind);
			return EXIT_SUCCESS;
		}
	} while (pci_next_dev(&devind, &vid, &did));

	USB_MSG("No EHCI controller found on PCI bus");
	return EXIT_FAILURE;
}


/*===========================================================================*
 *    ehci_pci_map                                                           *
 *===========================================================================*/
int
ehci_pci_map(ehci_pci_device *dev)
{
	hcd_reg1 caplength;

	DEBUG_DUMP;

	if (dev->base_addr == 0) {
		USB_MSG("EHCI BAR0 is zero, cannot map MMIO");
		return EXIT_FAILURE;
	}

	dev->cap_regs = hcd_os_regs_init(dev->base_addr, dev->mmio_size);
	if (dev->cap_regs == NULL) {
		USB_MSG("Failed to map EHCI MMIO at 0x%lx", dev->base_addr);
		return EXIT_FAILURE;
	}

	/*
	 * CAPLENGTH (8-bit register at offset 0) gives the byte offset from
	 * the capability register base to the first operational register.
	 */
	caplength = HCD_RD1(dev->cap_regs, EHCI_CAPLENGTH);
	dev->op_regs = (void *)((hcd_addr)dev->cap_regs + caplength);

	USB_MSG("EHCI MMIO mapped: cap_regs=%p op_regs=%p (CAPLENGTH=%u)",
		dev->cap_regs, dev->op_regs, (unsigned)caplength);

	return EXIT_SUCCESS;
}


/*===========================================================================*
 *    ehci_pci_unmap                                                         *
 *===========================================================================*/
void
ehci_pci_unmap(ehci_pci_device *dev)
{
	DEBUG_DUMP;

	if (dev->cap_regs != NULL) {
		if (hcd_os_regs_deinit(dev->base_addr, dev->mmio_size) != 0)
			USB_MSG("Warning: failed to unmap EHCI MMIO");
		dev->cap_regs = NULL;
		dev->op_regs  = NULL;
	}
}
