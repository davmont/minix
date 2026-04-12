/*
 * AMD64 USBD setup
 *
 * Entry point called by usbd.c (main) to initialize the platform-specific
 * USB Host Controller Drivers.  The PCI bus may have multiple USB controllers;
 * each is initialized independently and registers its own IRQ handler.
 *
 * Controller discovery order (each *_init() returns EXIT_SUCCESS or
 * EXIT_FAILURE and is independent of the others):
 *
 *   1. EHCI (USB 2.0, PCI class 0x0C/0x03/0x20) — Phase 1 complete
 *   2. OHCI companion (USB 1.1, class 0x0C/0x03/0x10) — Phase 3
 *   3. xHCI (USB 3.x, class 0x0C/0x03/0x30)           — Phase 4
 *
 * usbd_init_hcd() succeeds as long as at least one controller initializes.
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
	int ok = 0;

	DEBUG_DUMP;

	/*
	 * Initialize every controller type we support.  Failures are logged but
	 * do not abort initialization of the remaining controllers.
	 */
	if (ehci_init() == EXIT_SUCCESS) {
		USB_MSG("amd64: EHCI controller ready");
		ok++;
	} else {
		USB_MSG("amd64: EHCI init failed (no USB 2.0 HS support)");
	}

	/* Phase 3: ohci_init() goes here */
	/* Phase 4: xhci_init() goes here */

	return (ok > 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}


/*===========================================================================*
 *    usbd_deinit_hcd                                                        *
 *===========================================================================*/
void
usbd_deinit_hcd(void)
{
	DEBUG_DUMP;

	/* Deinit in reverse init order */
	/* Phase 4: xhci_deinit() goes here */
	/* Phase 3: ohci_deinit() goes here */
	ehci_deinit();
}
