/*
 * xHCI (Extensible Host Controller Interface) core driver — Phase 4a.
 *
 * Architecture mirrors ehci_core.c / ohci_core.c:
 *   - xhci_pci.c   — PCI discovery and MMIO mapping
 *   - xhci_mem.c   — DMA-accessible DCBAA / command ring / event ring / ERST
 *   - xhci_core.c  — controller bring-up (this file)
 *
 * Phase 4a scope:
 *   - Find the xHCI controller on PCI and map its register windows
 *     (capability / operational / runtime / doorbell, derived from
 *     CAPLENGTH / RTSOFF / DBOFF).
 *   - Take ownership from BIOS via the USB Legacy Support extended cap.
 *   - Reset the host controller and wait for it to become ready.
 *   - Program DCBAAP, the command ring (CRCR), the primary event ring
 *     (ERST / ERDP / ERSTBA) and any required scratchpad buffers.
 *   - Set Run/Stop and confirm the controller leaves the Halted state.
 *   - Log the root-hub ports and their connect status.
 *
 * Deferred to Phase 4b (the transfer engine):
 *   - Slot/Input/Device contexts, per-endpoint transfer rings, the command
 *     and event TRB protocol (Enable Slot / Address Device / Configure
 *     Endpoint), and the hcd_driver_state operation table.  Because none of
 *     those are wired yet, no IRQ handler is attached and no port-connect
 *     events are delivered: the controller simply runs.
 *
 * References:
 *   eXtensible Host Controller Interface for USB (xHCI), rev 1.2, §4.2
 *   (host controller initialization).
 */

#include <stdlib.h>
#include <string.h>

#include <minix/drivers.h>

#include <usbd/hcd_common.h>
#include <usbd/hcd_interface.h>
#include <usbd/usbd_common.h>

#include "xhci_core.h"
#include "xhci_mem.h"
#include "xhci_pci.h"
#include "xhci_regs.h"
#include "xhci_structs.h"


/*===========================================================================*
 *    Module-level state                                                     *
 *===========================================================================*/
static xhci_pci_device	xhci_dev;

/* Register window bases, derived from the capability registers. */
static void *xhci_cap;		/* capability registers (== xhci_dev.regs) */
static void *xhci_op;		/* operational registers (cap + CAPLENGTH)  */
static void *xhci_rt;		/* runtime registers (cap + RTSOFF)         */
static void *xhci_db;		/* doorbell array (cap + DBOFF)             */

static unsigned xhci_max_slots;
static unsigned xhci_max_ports;


/*===========================================================================*
 *    Helpers                                                                *
 *===========================================================================*/
#define XHCI_REGS_AT(base, off)	((void *)((char *)(base) + (off)))

static void
xhci_wr64(void *base, unsigned off, uint64_t val)
{
	HCD_WR4(base, off,     (hcd_reg4)(val & 0xFFFFFFFFu));
	HCD_WR4(base, off + 4, (hcd_reg4)(val >> 32));
}

/*
 * Take ownership of the controller from the BIOS via the USB Legacy Support
 * extended capability (if present).
 */
static void
xhci_bios_handoff(void)
{
	hcd_reg4 hcc1 = HCD_RD4(xhci_cap, XHCI_CAP_HCCPARAMS1);
	unsigned xecp = XHCI_HCC1_XECP(hcc1);	/* in dwords from cap base */
	void *cap;
	int guard;

	if (xecp == 0)
		return;

	cap = XHCI_REGS_AT(xhci_cap, xecp * 4u);

	for (guard = 0; guard < 64; guard++) {
		hcd_reg4 v = HCD_RD4(cap, 0);
		unsigned id   = XHCI_ECAP_ID(v);
		unsigned next = XHCI_ECAP_NEXT(v);

		if (id == XHCI_ECAP_ID_LEGACY) {
			int i;
			/* Request OS ownership, wait for BIOS to release */
			HCD_WR4(cap, 0, v | XHCI_LEGSUP_OS_OWNED);
			for (i = 0; i < 1000; i++) {
				v = HCD_RD4(cap, 0);
				if (!(v & XHCI_LEGSUP_BIOS_OWNED))
					break;
				hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(1));
			}
			USB_MSG("xHCI: BIOS hand-off %s",
				(v & XHCI_LEGSUP_BIOS_OWNED)
				? "timed out" : "complete");
			return;
		}

		if (next == 0)
			break;
		cap = XHCI_REGS_AT(cap, next * 4u);
	}
}

/*
 * Reset the host controller and wait for it to become ready.
 */
static int
xhci_hc_reset(void)
{
	int timeout;
	hcd_reg4 cmd;

	/* Wait for Controller-Not-Ready to clear before touching anything */
	for (timeout = 1000; timeout > 0; timeout--) {
		if (!(HCD_RD4(xhci_op, XHCI_OP_USBSTS) & XHCI_STS_CNR))
			break;
		hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(1));
	}

	/* Make sure the HC is stopped before reset */
	cmd = HCD_RD4(xhci_op, XHCI_OP_USBCMD);
	HCD_CLR(cmd, XHCI_CMD_RS);
	HCD_WR4(xhci_op, XHCI_OP_USBCMD, cmd);
	for (timeout = 1000; timeout > 0; timeout--) {
		if (HCD_RD4(xhci_op, XHCI_OP_USBSTS) & XHCI_STS_HCH)
			break;
		hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(1));
	}

	/* Assert Host Controller Reset */
	cmd = HCD_RD4(xhci_op, XHCI_OP_USBCMD);
	HCD_SET(cmd, XHCI_CMD_HCRST);
	HCD_WR4(xhci_op, XHCI_OP_USBCMD, cmd);

	for (timeout = 1000; timeout > 0; timeout--) {
		cmd = HCD_RD4(xhci_op, XHCI_OP_USBCMD);
		if (!(cmd & XHCI_CMD_HCRST) &&
		    !(HCD_RD4(xhci_op, XHCI_OP_USBSTS) & XHCI_STS_CNR))
			break;
		hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(1));
	}

	if (timeout == 0) {
		USB_MSG("xHCI: HC reset timed out");
		return EXIT_FAILURE;
	}

	USB_MSG("xHCI: HC reset complete");
	return EXIT_SUCCESS;
}

/*
 * Program the operational/runtime registers and start the controller.
 */
static void
xhci_hc_start(void)
{
	struct xhci_trb *cmd_ring = xhci_cmd_ring_virt();
	struct xhci_erst_entry *erst = xhci_erst_virt();
	hcd_reg4 hcs2;
	int n_scratch, timeout;
	phys_bytes sp;

	/* MaxSlotsEn = MaxSlots */
	HCD_WR4(xhci_op, XHCI_OP_CONFIG, xhci_max_slots);

	/* Device Context Base Address Array */
	xhci_wr64(xhci_op, XHCI_OP_DCBAAP, (uint64_t)xhci_dcbaa_phys());

	/* Scratchpad buffers, if the controller requires any */
	hcs2 = HCD_RD4(xhci_cap, XHCI_CAP_HCSPARAMS2);
	n_scratch = (int)XHCI_HCS2_MAX_SCRATCHPAD(hcs2);
	if (n_scratch > 0) {
		sp = xhci_mem_scratchpad(n_scratch);
		xhci_dcbaa_virt()[0] = (uint64_t)sp;
	}

	/* Command ring: terminate the segment with a Link TRB back to start.
	 * The initial producer cycle state is 1 (matches CRCR.RCS below). */
	memset(cmd_ring, 0, XHCI_RING_TRBS * sizeof(struct xhci_trb));
	cmd_ring[XHCI_RING_TRBS - 1].param_lo =
		(hcd_reg4)xhci_cmd_ring_phys();
	cmd_ring[XHCI_RING_TRBS - 1].param_hi =
		(hcd_reg4)((uint64_t)xhci_cmd_ring_phys() >> 32);
	cmd_ring[XHCI_RING_TRBS - 1].control =
		XHCI_TRB_TYPE(XHCI_TRB_TYPE_LINK) | XHCI_TRB_TC;
	xhci_wr64(xhci_op, XHCI_OP_CRCR,
		  (uint64_t)xhci_cmd_ring_phys() | XHCI_CRCR_RCS);

	/* Primary event ring: one segment described by a single ERST entry */
	memset(erst, 0, sizeof(*erst));
	erst->base_lo = (hcd_reg4)xhci_event_ring_phys();
	erst->base_hi = (hcd_reg4)((uint64_t)xhci_event_ring_phys() >> 32);
	erst->size    = XHCI_RING_TRBS;

	HCD_WR4(xhci_rt, XHCI_RT_IR0 + XHCI_IR_ERSTSZ, XHCI_ERST_ENTRIES);
	xhci_wr64(xhci_rt, XHCI_RT_IR0 + XHCI_IR_ERDP,
		  (uint64_t)xhci_event_ring_phys());
	xhci_wr64(xhci_rt, XHCI_RT_IR0 + XHCI_IR_ERSTBA,
		  (uint64_t)xhci_erst_phys());

	/* Run the controller (interrupts stay disabled in Phase 4a) */
	HCD_WR4(xhci_op, XHCI_OP_USBCMD, XHCI_CMD_RS);

	for (timeout = 1000; timeout > 0; timeout--) {
		if (!(HCD_RD4(xhci_op, XHCI_OP_USBSTS) & XHCI_STS_HCH))
			break;
		hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(1));
	}

	if (HCD_RD4(xhci_op, XHCI_OP_USBSTS) & XHCI_STS_HCH)
		USB_MSG("xHCI: warning, controller still halted after RS");
	else
		USB_MSG("xHCI: controller running");
}

/*
 * Power and log the root-hub ports.
 */
static void
xhci_scan_ports(void)
{
	unsigned i;

	for (i = 0; i < xhci_max_ports; i++) {
		hcd_reg4 portsc = HCD_RD4(xhci_op, XHCI_OP_PORTSC(i));

		/* Power the port if it is not already powered */
		if (!(portsc & XHCI_PORTSC_PP)) {
			HCD_WR4(xhci_op, XHCI_OP_PORTSC(i),
				(portsc & ~XHCI_PORTSC_RW1CS_MASK)
				| XHCI_PORTSC_PP);
			hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(20));
			portsc = HCD_RD4(xhci_op, XHCI_OP_PORTSC(i));
		}

		if (portsc & XHCI_PORTSC_CCS)
			USB_MSG("xHCI: port %u: device connected "
				"(speed id %u, portsc=0x%08x)",
				i, XHCI_PORTSC_SPEED(portsc), portsc);
	}
}


/*===========================================================================*
 *    xhci_init                                                              *
 *===========================================================================*/
int
xhci_init(void)
{
	hcd_reg4 cap0, hcs1, dboff, rtsoff;

	DEBUG_DUMP;

	memset(&xhci_dev, 0, sizeof(xhci_dev));

	/* 1. Find and map the controller */
	if (xhci_pci_find(&xhci_dev) != EXIT_SUCCESS)
		return EXIT_FAILURE;
	if (xhci_pci_map(&xhci_dev) != EXIT_SUCCESS)
		return EXIT_FAILURE;

	/* 2. Derive the register windows from the capability registers */
	xhci_cap = xhci_dev.regs;
	cap0   = HCD_RD4(xhci_cap, XHCI_CAP_CAPLENGTH);
	dboff  = HCD_RD4(xhci_cap, XHCI_CAP_DBOFF)  & XHCI_DBOFF_MASK;
	rtsoff = HCD_RD4(xhci_cap, XHCI_CAP_RTSOFF) & XHCI_RTSOFF_MASK;
	xhci_op = XHCI_REGS_AT(xhci_cap, XHCI_CAPLENGTH(cap0));
	xhci_rt = XHCI_REGS_AT(xhci_cap, rtsoff);
	xhci_db = XHCI_REGS_AT(xhci_cap, dboff);
	(void)xhci_db;	/* used by the Phase 4b transfer engine */

	hcs1 = HCD_RD4(xhci_cap, XHCI_CAP_HCSPARAMS1);
	xhci_max_slots = XHCI_HCS1_MAXSLOTS(hcs1);
	xhci_max_ports = XHCI_HCS1_MAXPORTS(hcs1);
	if (xhci_max_ports > 255)
		xhci_max_ports = 255;

	USB_MSG("xHCI: HCIVERSION=0x%04x CAPLENGTH=%u, %u slot(s), %u port(s)",
		XHCI_HCIVERSION(cap0), XHCI_CAPLENGTH(cap0),
		xhci_max_slots, xhci_max_ports);

	/* 3. Allocate DMA structures */
	if (xhci_mem_init() != EXIT_SUCCESS) {
		xhci_pci_unmap(&xhci_dev);
		return EXIT_FAILURE;
	}

	/* 4. Take ownership from BIOS and reset */
	xhci_bios_handoff();
	if (xhci_hc_reset() != EXIT_SUCCESS) {
		xhci_mem_deinit();
		xhci_pci_unmap(&xhci_dev);
		return EXIT_FAILURE;
	}

	/* 5. Program registers and start the controller */
	xhci_hc_start();

	/* 6. Power and report the root-hub ports */
	xhci_scan_ports();

	USB_MSG("xHCI: controller initialized (Phase 4a: bring-up only, "
		"transfers not yet supported)");
	return EXIT_SUCCESS;
}


/*===========================================================================*
 *    xhci_deinit                                                            *
 *===========================================================================*/
void
xhci_deinit(void)
{
	DEBUG_DUMP;

	if (xhci_op != NULL) {
		/* Stop the controller */
		HCD_WR4(xhci_op, XHCI_OP_USBCMD, 0);
	}

	xhci_mem_deinit();
	xhci_pci_unmap(&xhci_dev);

	xhci_cap = xhci_op = xhci_rt = xhci_db = NULL;
}
