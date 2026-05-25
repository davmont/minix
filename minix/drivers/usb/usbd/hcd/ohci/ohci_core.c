/*
 * OHCI (Open Host Controller Interface) core driver — Phase 3a (scaffolding).
 *
 * Architecture mirrors ehci_core.c:
 *   - ohci_pci.c     — PCI discovery and MMIO mapping
 *   - ohci_mem.c     — DMA-accessible HCCA / ED / TD / data-buffer pools
 *   - ohci_core.c    — hcd_driver_state operations (this file)
 *
 * Phase 3a scope (this commit):
 *   - Find OHCI controller on PCI bus
 *   - Map MMIO registers
 *   - Allocate DMA memory pools
 *   - Read HcRevision / HcRhDescriptorA and log them
 *   - Leave the controller in whatever state we found it; do not yet
 *     touch HcControl, HcCommandStatus, HcInterruptEnable
 *
 * Deferred to later phases:
 *   - Phase 3b: BIOS handoff (HcControl.IR), software reset (HCR), set
 *     HcFmInterval / HcPeriodicStart / HcLSThreshold, install HCCA,
 *     transition to USBOPERATIONAL.
 *   - Phase 3c: port power on, port reset/connect detection, plumb
 *     hcd_update_port() so usbd's enumeration thread runs.
 *   - Phase 3d: ED/TD list management, ISR, hcd_driver_state callbacks
 *     (setup_stage, rx/tx, etc).
 *
 * References:
 *   OpenHCI Open Host Controller Interface Specification, rev 1.0a
 *   USB 1.1 Specification §11 (root hub semantics)
 */

#include <stdlib.h>
#include <string.h>

#include <minix/drivers.h>

#include <usbd/hcd_common.h>
#include <usbd/hcd_interface.h>
#include <usbd/usbd_common.h>

#include "ohci_core.h"
#include "ohci_mem.h"
#include "ohci_pci.h"
#include "ohci_regs.h"
#include "ohci_structs.h"


/*===========================================================================*
 *    Module-level state                                                     *
 *===========================================================================*/
static ohci_pci_device	ohci_dev;
static hcd_driver_state	ohci_driver;


/*===========================================================================*
 *    ohci_init                                                              *
 *===========================================================================*/
int
ohci_init(void)
{
	hcd_reg4 rev, rhda;
	int n_ports;

	DEBUG_DUMP;

	memset(&ohci_dev,    0, sizeof(ohci_dev));
	memset(&ohci_driver, 0, sizeof(ohci_driver));

	/* 1. Find OHCI controller on PCI bus */
	if (ohci_pci_find(&ohci_dev) != EXIT_SUCCESS)
		return EXIT_FAILURE;

	/* 2. Map MMIO registers */
	if (ohci_pci_map(&ohci_dev) != EXIT_SUCCESS)
		return EXIT_FAILURE;

	/* 3. Allocate DMA memory pool */
	if (ohci_mem_init() != EXIT_SUCCESS) {
		ohci_pci_unmap(&ohci_dev);
		return EXIT_FAILURE;
	}

	/* 4. Log hardware information */
	rev      = HCD_RD4(ohci_dev.op_regs, OHCI_HC_REVISION);
	rhda     = HCD_RD4(ohci_dev.op_regs, OHCI_HC_RH_DESC_A);
	n_ports  = (int)OHCI_RHDA_NDP(rhda);

	USB_MSG("OHCI: HcRevision=0x%02x%s, %d root-hub port(s)",
		(unsigned)(rev & OHCI_REV_MASK),
		(rev & OHCI_REV_LEGACY) ? " (legacy emul)" : "",
		n_ports);

	/* 5. Wire driver identity.  Function-pointer callbacks remain NULL
	 *    until Phase 3d wires the hcd_driver_state operations table. */
	ohci_driver.controller_id = 1;	/* EHCI claims controller_id 0 */
	ohci_driver.private_data  = &ohci_dev;

	USB_MSG("OHCI: Phase 3a scaffolding ready "
		"(operational bring-up deferred)");
	return EXIT_SUCCESS;
}


/*===========================================================================*
 *    ohci_deinit                                                            *
 *===========================================================================*/
void
ohci_deinit(void)
{
	DEBUG_DUMP;

	ohci_mem_deinit();
	ohci_pci_unmap(&ohci_dev);
}
