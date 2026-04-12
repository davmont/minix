/*
 * AMD64 USBD setup
 *
 * Entry point called by usbd.c (main) to initialize the platform-specific
 * USB Host Controller Driver.  On amd64, USB is provided by a PCI-attached
 * EHCI controller (USB 2.0).  The PCI bus is enumerated by ehci_pci.c and
 * the controller is driven by ehci_core.c.
 *
 * Future work:
 *   - xHCI (USB 3.x) support for SuperSpeed devices.
 *   - Companion OHCI/UHCI hand-off for full- and low-speed devices.
 */

#include <usbd/hcd_platforms.h>
#include <usbd/usbd_common.h>
#include <usbd/usbd_interface.h>


/*===========================================================================*
 *    usbd_init_hcd                                                          *
 *===========================================================================*/
int
usbd_init_hcd(void)
{
	DEBUG_DUMP;

	USB_MSG("Initializing EHCI driver for amd64");
	return ehci_init();
}


/*===========================================================================*
 *    usbd_deinit_hcd                                                        *
 *===========================================================================*/
void
usbd_deinit_hcd(void)
{
	DEBUG_DUMP;

	ehci_deinit();
}
