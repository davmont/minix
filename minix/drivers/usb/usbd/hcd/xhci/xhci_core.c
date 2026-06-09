/*
 * xHCI (Extensible Host Controller Interface) core driver.
 *
 * Architecture mirrors ehci_core.c / ohci_core.c:
 *   - xhci_pci.c   — PCI discovery and MMIO mapping
 *   - xhci_mem.c   — DMA structures (DCBAA, command/event rings, ERST,
 *                    input/device contexts, EP0 + bulk transfer rings)
 *   - xhci_core.c  — controller bring-up + transfer engine (this file)
 *
 * Adapting xHCI to the staged hcd.c framework
 * --------------------------------------------
 * hcd.c drives enumeration as a series of control-transfer *stages*
 * (setup_stage, in/out_data_stage, in/out_status_stage), waiting for one
 * completion event per stage.  xHCI represents each control stage as its own
 * TRB (Setup / Data / Status), and a TRB with IOC generates exactly one
 * Transfer Event — so the per-stage model maps directly: each stage enqueues
 * one TRB with IOC and rings the doorbell; the ISR turns the resulting
 * Transfer Event into HCD_EVENT_ENDPOINT, which wakes the waiting stage.
 *
 * Two things do not map to a control transfer and are handled with xHCI
 * commands instead:
 *   - Addressing: xHCI assigns the address itself via the Address Device
 *     command (the HC sends SET_ADDRESS), and routes all traffic by Slot ID,
 *     not USB address.  reset_device issues Enable Slot + Address Device
 *     (BSR=1, "set up the slot but do not send SET_ADDRESS yet") so EP0
 *     control transfers work at address 0.  When the framework then issues
 *     the SET_ADDRESS control transfer, setup_stage intercepts it, runs
 *     reset_device runs Address Device, so this is a no-op transfer that just
 *     releases the framework's per-stage waits.
 *   - Bulk endpoints: before the first bulk transfer on an endpoint, a
 *     Configure Endpoint command adds that endpoint (and its transfer ring)
 *     to the slot; this is done lazily from rx_stage/tx_stage.
 *
 * Because QEMU's xHCI executes a control transfer as a whole TD (Setup [+
 * Data] + Status) rather than per stage, both commands and transfers are run
 * synchronously from the device thread: enqueue the TRBs, ring the doorbell,
 * then drain the event ring directly (cooperatively yielding) until the
 * completion event arrives.  The framework's per-stage hcd_device_wait() calls
 * are then released by up-ing the device's wait semaphore.
 *
 * Status: USB 3.0 SuperSpeed mass storage works end to end — Enable Slot,
 * Address Device, control enumeration (SET_CONFIGURATION) and bulk SCSI (BBB)
 * transfers all complete, and a USB 3.0 disk's blocks read back byte-for-byte
 * correct in QEMU (-device qemu-xhci + usb-storage).  (usb_storage needed a
 * one-line fix to skip SuperSpeed Endpoint Companion descriptors.)
 *
 * Limitations: one device at a time (single slot, single EP0 ring, one bulk
 * IN + one bulk OUT ring), matching the EHCI/OHCI Phase-1 scope.
 *
 * References: eXtensible Host Controller Interface for USB (xHCI) rev 1.2.
 */

#include <stdlib.h>
#include <string.h>

#include <minix/drivers.h>

#include <ddekit/semaphore.h>

#include <usbd/hcd_common.h>
#include <usbd/hcd_interface.h>
#include <usbd/usbd_common.h>

#include "xhci_core.h"
#include "xhci_mem.h"
#include "xhci_pci.h"
#include "xhci_regs.h"
#include "xhci_structs.h"


/*===========================================================================*
 *    Local constants                                                        *
 *===========================================================================*/
#define XHCI_CMD_TIMEOUT_MSEC	1000	/* command completion poll budget */
#define XHCI_PORT_RESET_MSEC	50
#define XHCI_LAST_RING_TRB	(XHCI_RING_TRBS - 1)	/* slot used by Link TRB */


/*===========================================================================*
 *    Module-level state                                                     *
 *===========================================================================*/
static xhci_pci_device	xhci_dev;
static hcd_driver_state	xhci_driver;
static int		xhci_irq_hook = -1;

/* Register windows derived from the capability registers */
static void *xhci_cap, *xhci_op, *xhci_rt, *xhci_db;
static unsigned xhci_max_slots, xhci_max_ports, xhci_ctx_size;

/* Producer rings: enqueue index + producer cycle state (PCS) */
struct xhci_ring_state { int enq; int cycle; };
static struct xhci_ring_state cmd_ring  = { 0, 1 };
static struct xhci_ring_state ep0_ring  = { 0, 1 };
static struct xhci_ring_state bin_ring  = { 0, 1 };
static struct xhci_ring_state bout_ring = { 0, 1 };

/* Event ring consumer: dequeue index + consumer cycle state (CCS) */
static int evt_deq;
static int evt_cycle;

/* Command completion handshake (written by ISR, polled by device thread) */
static volatile int      cmd_done;
static volatile hcd_reg4 cmd_cc;	/* completion code */
static volatile int      cmd_slot;	/* slot id from Enable Slot completion */

/* Last transfer event (for check_error / read_data) */
static volatile hcd_reg4 xfer_cc;	/* completion code */
static volatile hcd_reg4 xfer_residual;	/* untransferred bytes */
static volatile int      xfer_done;	/* set by process_events on a xfer evt */
static int               xfer_is_bulk;	/* deliver bulk evts via hcd_handle_event */
static hcd_reg1          xfer_ep;	/* endpoint of the in-flight bulk xfer */

/* Active device / transfer context */
static struct {
	int		slot_id;
	int		port_idx;
	hcd_reg4	speed;		/* PORTSC speed id */
	uint32_t	last_len;	/* requested length of last data stage */
	int		set_address;	/* current control xfer is SET_ADDRESS */
	int		bin_ep, bout_ep;/* configured bulk endpoint numbers, -1 */
} xhci_active;


/*===========================================================================*
 *    Forward declarations                                                   *
 *===========================================================================*/
static void  xhci_setup_device    (void *, hcd_reg1, hcd_reg1,
				   hcd_datatog *, hcd_datatog *);
static int   xhci_reset_device    (void *, hcd_speed *);
static void  xhci_setup_stage     (void *, hcd_ctrlrequest *);
static void  xhci_rx_stage        (void *, hcd_datarequest *);
static void  xhci_tx_stage        (void *, hcd_datarequest *);
static void  xhci_in_data_stage   (void *);
static void  xhci_out_data_stage  (void *);
static void  xhci_in_status_stage (void *);
static void  xhci_out_status_stage(void *);
static int   xhci_read_data       (void *, hcd_reg1 *, hcd_reg1);
static int   xhci_check_error     (void *, hcd_transfer, hcd_reg1,
				   hcd_direction);
static void  xhci_isr_init(void *);
static void  xhci_isr     (void *);
static void  xhci_process_events(void);
static void  xhci_scan_ports(void);

/* One-shot: deliver the connect for the first attached port */
static int xhci_scan_done;


/*===========================================================================*
 *    Register / context helpers                                             *
 *===========================================================================*/
#define XHCI_AT(base, off)	((void *)((char *)(base) + (off)))

static void
xhci_wr64(void *base, unsigned off, uint64_t val)
{
	HCD_WR4(base, off,     (hcd_reg4)(val & 0xFFFFFFFFu));
	HCD_WR4(base, off + 4, (hcd_reg4)(val >> 32));
}

/* Pointer to context entry 'idx' within a (32/64-byte-strided) context block */
static struct xhci_ctx *
xhci_ctx_at(uint8_t *base, int idx)
{
	return (struct xhci_ctx *)(base + idx * (int)xhci_ctx_size);
}

/* Device Context Index for endpoint number / direction (EP0 -> 1) */
static int
xhci_dci(int ep, int is_in)
{
	if (ep == 0)
		return 1;
	return 2 * ep + (is_in ? 1 : 0);
}

/* Ring the doorbell for a slot with the given DB target (DCI) */
static void
xhci_ring_doorbell(int slot, hcd_reg4 target)
{
	HCD_WR4(xhci_db, XHCI_DB(slot), target);
}


/*===========================================================================*
 *    Ring enqueue                                                           *
 *===========================================================================*/
/*
 * Append one TRB to a producer ring (command or transfer), inserting the
 * cycle bit and handling the Link-TRB wrap at the end of the segment.
 * The caller supplies param/status and the control word *without* the cycle
 * bit; this function ORs in the current PCS.  Returns the physical address of
 * the enqueued TRB.
 */
static phys_bytes
xhci_ring_enqueue(struct xhci_trb *ring, phys_bytes ring_phys,
		  struct xhci_ring_state *st,
		  hcd_reg4 param_lo, hcd_reg4 param_hi,
		  hcd_reg4 status, hcd_reg4 control)
{
	struct xhci_trb *trb = &ring[st->enq];
	phys_bytes trb_phys = ring_phys +
			      (phys_bytes)st->enq * sizeof(struct xhci_trb);

	trb->param_lo = param_lo;
	trb->param_hi = param_hi;
	trb->status   = status;
	/* Cycle bit last (HC reads it to detect ownership) */
	trb->control  = control | (st->cycle ? XHCI_TRB_C : 0);

	st->enq++;
	if (st->enq == XHCI_LAST_RING_TRB) {
		/* Program the Link TRB and wrap, toggling the cycle state */
		struct xhci_trb *link = &ring[XHCI_LAST_RING_TRB];
		link->param_lo = (hcd_reg4)ring_phys;
		link->param_hi = (hcd_reg4)((uint64_t)ring_phys >> 32);
		link->status   = 0;
		link->control  = XHCI_TRB_TYPE(XHCI_TRB_TYPE_LINK) |
				 XHCI_TRB_TC |
				 (st->cycle ? XHCI_TRB_C : 0);
		st->enq = 0;
		st->cycle ^= 1;
	}

	return trb_phys;
}


/*===========================================================================*
 *    Command ring                                                           *
 *===========================================================================*/
/*
 * Submit one command TRB and wait (cooperatively) for its Command Completion
 * event.  Returns the completion code (XHCI_TRB_CC_SUCCESS on success).
 */
static hcd_reg4
xhci_command(hcd_reg4 param_lo, hcd_reg4 param_hi, hcd_reg4 control)
{
	int budget;

	cmd_done = 0;
	cmd_cc   = 0;

	(void)xhci_ring_enqueue(xhci_cmd_ring_virt(), xhci_cmd_ring_phys(),
				&cmd_ring, param_lo, param_hi, 0, control);

	/* Ring the host controller's command doorbell (slot 0, target 0) */
	xhci_ring_doorbell(XHCI_DB_HOST, XHCI_DB_TARGET_CMD);

	/*
	 * Drain the event ring ourselves while waiting.  The HC posts the
	 * Command Completion to the ring by DMA, so this does not depend on a
	 * second interrupt being delivered.  Cooperative scheduling means
	 * xhci_process_events() cannot run concurrently in the ISR.
	 */
	for (budget = XHCI_CMD_TIMEOUT_MSEC; budget > 0; budget--) {
		xhci_process_events();
		if (cmd_done)
			return cmd_cc;
		hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(1));
	}

	USB_MSG("xHCI: command 0x%x timed out", XHCI_TRB_GET_TYPE(control));
	return 0;	/* 0 = invalid CC -> treated as failure */
}


/*===========================================================================*
 *    Slot / endpoint setup                                                  *
 *===========================================================================*/
/* Default EP0 max-packet size for a port speed id */
static unsigned
xhci_ep0_mps(hcd_reg4 speed)
{
	switch (speed) {
	case XHCI_SPEED_SUPER:	return 512;
	case XHCI_SPEED_HIGH:	return 64;
	case XHCI_SPEED_LOW:	return 8;
	default:		return 64;	/* full-speed: 8/16/32/64 */
	}
}

/*
 * Build the input context for Address Device: enable the slot and EP0
 * contexts, program the slot (speed, root-hub port) and EP0 (control ring).
 */
static void
xhci_build_address_input(void)
{
	uint8_t *in = xhci_input_ctx_virt();
	struct xhci_ctx *icc  = xhci_ctx_at(in, 0);	/* input control */
	struct xhci_ctx *slot = xhci_ctx_at(in, 1);	/* slot context  */
	struct xhci_ctx *ep0  = xhci_ctx_at(in, 2);	/* EP0 context   */
	unsigned mps = xhci_ep0_mps(xhci_active.speed);

	memset(in, 0, 3 * xhci_ctx_size);

	/* Add slot (A0) and EP0 (A1) */
	icc->dw[XHCI_ICC_ADD] = XHCI_ICC_ADD_BIT(0) | XHCI_ICC_ADD_BIT(1);

	/* Slot: route 0, speed, 1 context entry, root-hub port (1-based) */
	slot->dw[0] = XHCI_SLOT_DW0_ROUTE(0) |
		      XHCI_SLOT_DW0_SPEED(xhci_active.speed) |
		      XHCI_SLOT_DW0_CTX_ENTRIES(1);
	slot->dw[1] = XHCI_SLOT_DW1_RHPORT(xhci_active.port_idx + 1);

	/* EP0: control endpoint, error count 3, max packet, EP0 ring */
	ep0->dw[1] = XHCI_EP_DW1_CERR(3) |
		     XHCI_EP_DW1_TYPE(XHCI_EP_TYPE_CONTROL) |
		     XHCI_EP_DW1_MAXPKT(mps);
	ep0->dw[2] = (hcd_reg4)xhci_ep0_ring_phys() | XHCI_EP_DCS;
	ep0->dw[3] = (hcd_reg4)((uint64_t)xhci_ep0_ring_phys() >> 32);
	ep0->dw[4] = XHCI_EP_DW4_AVGTRB(8);
}


/*===========================================================================*
 *    Internal init helpers                                                  *
 *===========================================================================*/
static void
xhci_bios_handoff(void)
{
	hcd_reg4 hcc1 = HCD_RD4(xhci_cap, XHCI_CAP_HCCPARAMS1);
	unsigned xecp = XHCI_HCC1_XECP(hcc1);
	void *cap;
	int guard;

	if (xecp == 0)
		return;
	cap = XHCI_AT(xhci_cap, xecp * 4u);

	for (guard = 0; guard < 64; guard++) {
		hcd_reg4 v = HCD_RD4(cap, 0);
		unsigned id = XHCI_ECAP_ID(v), next = XHCI_ECAP_NEXT(v);

		if (id == XHCI_ECAP_ID_LEGACY) {
			int i;
			HCD_WR4(cap, 0, v | XHCI_LEGSUP_OS_OWNED);
			for (i = 0; i < 1000; i++) {
				v = HCD_RD4(cap, 0);
				if (!(v & XHCI_LEGSUP_BIOS_OWNED))
					break;
				hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(1));
			}
			return;
		}
		if (next == 0)
			break;
		cap = XHCI_AT(cap, next * 4u);
	}
}

static int
xhci_hc_reset(void)
{
	int t;
	hcd_reg4 cmd;

	for (t = 1000; t > 0; t--) {
		if (!(HCD_RD4(xhci_op, XHCI_OP_USBSTS) & XHCI_STS_CNR))
			break;
		hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(1));
	}
	cmd = HCD_RD4(xhci_op, XHCI_OP_USBCMD);
	HCD_CLR(cmd, XHCI_CMD_RS);
	HCD_WR4(xhci_op, XHCI_OP_USBCMD, cmd);
	for (t = 1000; t > 0; t--) {
		if (HCD_RD4(xhci_op, XHCI_OP_USBSTS) & XHCI_STS_HCH)
			break;
		hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(1));
	}

	cmd = HCD_RD4(xhci_op, XHCI_OP_USBCMD);
	HCD_SET(cmd, XHCI_CMD_HCRST);
	HCD_WR4(xhci_op, XHCI_OP_USBCMD, cmd);
	for (t = 1000; t > 0; t--) {
		if (!(HCD_RD4(xhci_op, XHCI_OP_USBCMD) & XHCI_CMD_HCRST) &&
		    !(HCD_RD4(xhci_op, XHCI_OP_USBSTS) & XHCI_STS_CNR))
			break;
		hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(1));
	}
	if (t == 0) {
		USB_MSG("xHCI: HC reset timed out");
		return EXIT_FAILURE;
	}
	USB_MSG("xHCI: HC reset complete");
	return EXIT_SUCCESS;
}

static void
xhci_hc_start(void)
{
	struct xhci_trb *cr = xhci_cmd_ring_virt();
	struct xhci_erst_entry *erst = xhci_erst_virt();
	hcd_reg4 hcs2;
	int n_scratch;

	HCD_WR4(xhci_op, XHCI_OP_CONFIG, xhci_max_slots);
	xhci_wr64(xhci_op, XHCI_OP_DCBAAP, (uint64_t)xhci_dcbaa_phys());

	hcs2 = HCD_RD4(xhci_cap, XHCI_CAP_HCSPARAMS2);
	n_scratch = (int)XHCI_HCS2_MAX_SCRATCHPAD(hcs2);
	if (n_scratch > 0)
		xhci_dcbaa_virt()[0] = (uint64_t)xhci_mem_scratchpad(n_scratch);

	/* Command ring: Link TRB at the end wraps back to start, toggle cycle */
	memset(cr, 0, XHCI_RING_TRBS * sizeof(struct xhci_trb));
	cr[XHCI_LAST_RING_TRB].param_lo = (hcd_reg4)xhci_cmd_ring_phys();
	cr[XHCI_LAST_RING_TRB].param_hi =
		(hcd_reg4)((uint64_t)xhci_cmd_ring_phys() >> 32);
	cr[XHCI_LAST_RING_TRB].control =
		XHCI_TRB_TYPE(XHCI_TRB_TYPE_LINK) | XHCI_TRB_TC;
	cmd_ring.enq = 0;
	cmd_ring.cycle = 1;
	xhci_wr64(xhci_op, XHCI_OP_CRCR,
		  (uint64_t)xhci_cmd_ring_phys() | XHCI_CRCR_RCS);

	/* Primary event ring */
	memset(erst, 0, sizeof(*erst));
	erst->base_lo = (hcd_reg4)xhci_event_ring_phys();
	erst->base_hi = (hcd_reg4)((uint64_t)xhci_event_ring_phys() >> 32);
	erst->size    = XHCI_RING_TRBS;
	evt_deq   = 0;
	evt_cycle = 1;
	HCD_WR4(xhci_rt, XHCI_RT_IR0 + XHCI_IR_ERSTSZ, XHCI_ERST_ENTRIES);
	xhci_wr64(xhci_rt, XHCI_RT_IR0 + XHCI_IR_ERDP,
		  (uint64_t)xhci_event_ring_phys());
	xhci_wr64(xhci_rt, XHCI_RT_IR0 + XHCI_IR_ERSTBA,
		  (uint64_t)xhci_erst_phys());

	/* Enable interrupter 0 and run with interrupts on */
	HCD_WR4(xhci_rt, XHCI_RT_IR0 + XHCI_IR_IMOD, 0);
	HCD_WR4(xhci_rt, XHCI_RT_IR0 + XHCI_IR_IMAN, XHCI_IMAN_IE);
	HCD_WR4(xhci_op, XHCI_OP_USBCMD, XHCI_CMD_RS | XHCI_CMD_INTE);

	{
		int t;
		for (t = 1000; t > 0; t--) {
			if (!(HCD_RD4(xhci_op, XHCI_OP_USBSTS) & XHCI_STS_HCH))
				break;
			hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(1));
		}
	}

	/*
	 * Power the root-hub ports.  A device already attached generates a
	 * Port Status Change Event, which the ISR turns into the connect once
	 * usbd is fully running (the connect must not be delivered from init,
	 * before the URB scheduler and device thread exist).
	 */
	{
		unsigned i;
		for (i = 0; i < xhci_max_ports; i++) {
			hcd_reg4 ps = HCD_RD4(xhci_op, XHCI_OP_PORTSC(i));
			if (!(ps & XHCI_PORTSC_PP))
				HCD_WR4(xhci_op, XHCI_OP_PORTSC(i),
					(ps & ~XHCI_PORTSC_RW1CS_MASK) |
					XHCI_PORTSC_PP);
		}
	}

	USB_MSG("xHCI: controller running");
}


/*===========================================================================*
 *    ISR / event processing                                                 *
 *===========================================================================*/
static void
xhci_isr_init(void *priv)
{
	(void)priv;
}

static void
xhci_isr(void *priv)
{
	hcd_reg4 sts;

	(void)priv;

	sts = HCD_RD4(xhci_op, XHCI_OP_USBSTS);
	if (!(sts & XHCI_STS_EINT))
		return;	/* not ours */

	/* Ack EINT (RW1C) and the interrupter's IP bit */
	HCD_WR4(xhci_op, XHCI_OP_USBSTS, XHCI_STS_EINT);
	HCD_WR4(xhci_rt, XHCI_RT_IR0 + XHCI_IR_IMAN,
		XHCI_IMAN_IE | XHCI_IMAN_IP);

	xhci_process_events();
}

/*
 * Drain the event ring, dispatching each event by type.  The single consumer
 * (this function, called only from the ISR) advances the dequeue pointer and
 * updates ERDP.
 */
static void
xhci_process_events(void)
{
	struct xhci_trb *er = xhci_event_ring_virt();
	int handled = 0;

	for (;;) {
		struct xhci_trb *ev = &er[evt_deq];
		int cbit = (ev->control & XHCI_TRB_C) ? 1 : 0;
		int type;

		if (cbit != evt_cycle)
			break;	/* HC has not produced this entry yet */

		type = (int)XHCI_TRB_GET_TYPE(ev->control);

		switch (type) {
		case XHCI_TRB_TYPE_CMD_COMPLETION:
			cmd_cc   = XHCI_TRB_GET_CC(ev->status);
			cmd_slot = (int)XHCI_TRB_GET_SLOT(ev->control);
			cmd_done = 1;
			break;

		case XHCI_TRB_TYPE_TRANSFER_EVENT:
			/* Record the first/only data-bearing event's residual;
			 * keep cc.  Control transfers poll xfer_done directly;
			 * bulk transfers are woken via hcd_handle_event. */
			if (!xfer_done)
				xfer_residual = XHCI_TRB_GET_TXLEN(ev->status);
			xfer_cc   = XHCI_TRB_GET_CC(ev->status);
			xfer_done = 1;
			if (xfer_is_bulk)
				hcd_handle_event(
				    xhci_driver.port_device[xhci_active.port_idx],
				    HCD_EVENT_ENDPOINT, xfer_ep);
			break;

		case XHCI_TRB_TYPE_PORT_STATUS_EVENT:
			/* A root-hub port changed.  Deliver the connect from
			 * here (interrupt context, controller running) rather
			 * than from init. */
			if (!xhci_scan_done)
				xhci_scan_ports();
			break;

		default:
			break;
		}

		/* Advance dequeue, wrapping + toggling at the segment end */
		evt_deq++;
		if (evt_deq == XHCI_RING_TRBS) {
			evt_deq = 0;
			evt_cycle ^= 1;
		}
		handled++;
	}

	if (handled) {
		/* Update ERDP to the current dequeue position, clear EHB */
		uint64_t erdp = (uint64_t)xhci_event_ring_phys() +
			(uint64_t)evt_deq * sizeof(struct xhci_trb);
		xhci_wr64(xhci_rt, XHCI_RT_IR0 + XHCI_IR_ERDP,
			  erdp | XHCI_ERDP_EHB);

		/* Fallback: a device present before we started running does not
		 * generate a fresh Port Status Change, so scan on the first
		 * event of any kind. */
		if (!xhci_scan_done)
			xhci_scan_ports();
	}
}


/*===========================================================================*
 *    Port scan (one-shot connect delivery)                                  *
 *===========================================================================*/
static void
xhci_scan_ports(void)
{
	unsigned i;

	for (i = 0; i < xhci_max_ports; i++) {
		hcd_reg4 ps = HCD_RD4(xhci_op, XHCI_OP_PORTSC(i));

		/* Ack any change bits (RW1CS) without disturbing PP/PED */
		if (ps & XHCI_PORTSC_CHANGE_MASK)
			HCD_WR4(xhci_op, XHCI_OP_PORTSC(i),
				(ps & ~XHCI_PORTSC_RW1CS_MASK) |
				XHCI_PORTSC_CHANGE_MASK);

		if (ps & XHCI_PORTSC_CCS) {
			USB_MSG("xHCI: device connected on port %u "
				"(speed id %u)", i, XHCI_PORTSC_SPEED(ps));
			xhci_scan_done = 1;
			xhci_active.port_idx = (int)i;
			xhci_active.speed = XHCI_PORTSC_SPEED(ps);
			hcd_update_port(&xhci_driver, HCD_EVENT_CONNECTED,
					(int)i);
			hcd_handle_event(xhci_driver.port_device[i],
					 HCD_EVENT_CONNECTED, 0);
			return;	/* single-device driver: first port only */
		}
	}
}


/*===========================================================================*
 *    xhci_init                                                              *
 *===========================================================================*/
int
xhci_init(void)
{
	hcd_reg4 cap0, hcs1, hcc1;

	DEBUG_DUMP;

	memset(&xhci_dev, 0, sizeof(xhci_dev));
	memset(&xhci_driver, 0, sizeof(xhci_driver));
	memset(&xhci_active, 0, sizeof(xhci_active));
	xhci_active.slot_id = 0;
	xhci_active.bin_ep = xhci_active.bout_ep = -1;
	xhci_scan_done = 0;

	if (xhci_pci_find(&xhci_dev) != EXIT_SUCCESS)
		return EXIT_FAILURE;
	if (xhci_pci_map(&xhci_dev) != EXIT_SUCCESS)
		return EXIT_FAILURE;

	xhci_cap = xhci_dev.regs;
	cap0 = HCD_RD4(xhci_cap, XHCI_CAP_CAPLENGTH);
	xhci_op = XHCI_AT(xhci_cap, XHCI_CAPLENGTH(cap0));
	xhci_rt = XHCI_AT(xhci_cap,
			  HCD_RD4(xhci_cap, XHCI_CAP_RTSOFF) & XHCI_RTSOFF_MASK);
	xhci_db = XHCI_AT(xhci_cap,
			  HCD_RD4(xhci_cap, XHCI_CAP_DBOFF) & XHCI_DBOFF_MASK);

	hcs1 = HCD_RD4(xhci_cap, XHCI_CAP_HCSPARAMS1);
	hcc1 = HCD_RD4(xhci_cap, XHCI_CAP_HCCPARAMS1);
	xhci_max_slots = XHCI_HCS1_MAXSLOTS(hcs1);
	xhci_max_ports = XHCI_HCS1_MAXPORTS(hcs1);
	if (xhci_max_ports > 255)
		xhci_max_ports = 255;
	xhci_ctx_size = (hcc1 & XHCI_HCC1_CSZ) ? 64 : 32;

	USB_MSG("xHCI: HCIVERSION=0x%04x CAPLENGTH=%u, %u slot(s), %u port(s), "
		"ctx=%uB", XHCI_HCIVERSION(cap0), XHCI_CAPLENGTH(cap0),
		xhci_max_slots, xhci_max_ports, xhci_ctx_size);

	if (xhci_mem_init() != EXIT_SUCCESS) {
		xhci_pci_unmap(&xhci_dev);
		return EXIT_FAILURE;
	}

	xhci_irq_hook = hcd_os_interrupt_attach(xhci_dev.irq, xhci_isr_init,
						xhci_isr, &xhci_driver);
	if (xhci_irq_hook != EXIT_SUCCESS) {
		USB_MSG("xHCI: failed to attach IRQ %d", xhci_dev.irq);
		xhci_mem_deinit();
		xhci_pci_unmap(&xhci_dev);
		return EXIT_FAILURE;
	}

	/* Wire the transfer operations */
	xhci_driver.controller_id    = 2;	/* EHCI=0, OHCI=1 */
	xhci_driver.setup_device     = xhci_setup_device;
	xhci_driver.reset_device     = xhci_reset_device;
	xhci_driver.setup_stage      = xhci_setup_stage;
	xhci_driver.rx_stage         = xhci_rx_stage;
	xhci_driver.tx_stage         = xhci_tx_stage;
	xhci_driver.in_data_stage    = xhci_in_data_stage;
	xhci_driver.out_data_stage   = xhci_out_data_stage;
	xhci_driver.in_status_stage  = xhci_in_status_stage;
	xhci_driver.out_status_stage = xhci_out_status_stage;
	xhci_driver.read_data        = xhci_read_data;
	xhci_driver.check_error      = xhci_check_error;
	xhci_driver.private_data     = &xhci_dev;

	xhci_bios_handoff();
	if (xhci_hc_reset() != EXIT_SUCCESS) {
		xhci_mem_deinit();
		xhci_pci_unmap(&xhci_dev);
		return EXIT_FAILURE;
	}
	xhci_hc_start();

	hcd_os_interrupt_enable(xhci_dev.irq);

	USB_MSG("xHCI: controller initialized (irq %d)", xhci_dev.irq);

	/*
	 * Enqueue a No-Op command and ring the doorbell.  Its Command
	 * Completion event guarantees an interrupt shortly, once usbd is
	 * running, which is where the ISR performs the initial port scan and
	 * delivers the connect (the connect must not be delivered from init,
	 * before the URB scheduler / device thread exist).
	 */
	(void)xhci_ring_enqueue(xhci_cmd_ring_virt(), xhci_cmd_ring_phys(),
				&cmd_ring, 0, 0, 0,
				XHCI_TRB_TYPE(XHCI_TRB_TYPE_NOOP_CMD));
	xhci_ring_doorbell(XHCI_DB_HOST, XHCI_DB_TARGET_CMD);

	return EXIT_SUCCESS;
}


/*===========================================================================*
 *    xhci_deinit                                                            *
 *===========================================================================*/
void
xhci_deinit(void)
{
	DEBUG_DUMP;

	if (xhci_op != NULL)
		HCD_WR4(xhci_op, XHCI_OP_USBCMD, 0);
	if (xhci_irq_hook == EXIT_SUCCESS) {
		hcd_os_interrupt_disable(xhci_dev.irq);
		hcd_os_interrupt_detach(xhci_dev.irq);
		xhci_irq_hook = -1;
	}
	xhci_mem_deinit();
	xhci_pci_unmap(&xhci_dev);
	xhci_cap = xhci_op = xhci_rt = xhci_db = NULL;
}


/*===========================================================================*
 *    hcd_driver_state operations                                            *
 *===========================================================================*/

/* Control IN data already copied to the caller once? (reset each setup) */
static int xhci_data_consumed;

/*
 * Satisfy one of the framework's per-stage hcd_device_wait() calls.  Control
 * transfers are performed synchronously in setup_stage (QEMU's xHCI executes a
 * control transfer as a whole TD, not per stage), so the data/status stage
 * functions and setup_stage just release the device's wait semaphore directly.
 */
static void
xhci_wake_device(void)
{
	hcd_device_state *dev = xhci_driver.port_device[xhci_active.port_idx];

	if (dev != NULL && dev->lock != NULL)
		ddekit_sem_up(dev->lock);
}

/*
 * Run a complete control transfer as one xHCI TD (Setup [+ Data] + Status)
 * and wait for completion by draining the event ring directly.
 */
static void
xhci_control_td(hcd_ctrlrequest *req)
{
	uint64_t setup_data;
	int is_in = ((req->bRequestType & UT_READ) == UT_READ);
	int has_data = (req->wLength > 0);
	hcd_reg4 trt;
	int budget;

	xfer_is_bulk = 0;
	xfer_done = 0;
	xfer_cc = 0;
	xfer_residual = 0;
	xhci_data_consumed = 0;
	xhci_active.last_len = req->wLength;

	if (!has_data)
		trt = XHCI_TRB_TRT_NODATA;
	else
		trt = is_in ? XHCI_TRB_TRT_IN : XHCI_TRB_TRT_OUT;

	/* Setup Stage TRB (immediate data, no interrupt) */
	memcpy(&setup_data, req, sizeof(setup_data));
	(void)xhci_ring_enqueue(xhci_ep0_ring_virt(), xhci_ep0_ring_phys(),
		&ep0_ring,
		(hcd_reg4)(setup_data & 0xFFFFFFFFu),
		(hcd_reg4)(setup_data >> 32),
		XHCI_TRB_TXLEN(8),
		XHCI_TRB_TYPE(XHCI_TRB_TYPE_SETUP_STAGE) | trt | XHCI_TRB_IDT);

	/* Data Stage TRB (IN only; control OUT-with-data is unused in enum) */
	if (has_data && is_in)
		(void)xhci_ring_enqueue(xhci_ep0_ring_virt(),
			xhci_ep0_ring_phys(), &ep0_ring,
			(hcd_reg4)xhci_data_buf_phys(),
			(hcd_reg4)((uint64_t)xhci_data_buf_phys() >> 32),
			XHCI_TRB_TXLEN(req->wLength),
			XHCI_TRB_TYPE(XHCI_TRB_TYPE_DATA_STAGE) |
			XHCI_TRB_DIR_IN | XHCI_TRB_ISP);

	/* Status Stage TRB (IOC; direction opposite the data stage) */
	(void)xhci_ring_enqueue(xhci_ep0_ring_virt(), xhci_ep0_ring_phys(),
		&ep0_ring, 0, 0, 0,
		XHCI_TRB_TYPE(XHCI_TRB_TYPE_STATUS_STAGE) |
		((has_data && is_in) ? 0u : XHCI_TRB_DIR_IN) | XHCI_TRB_IOC);

	/* Ring the EP0 doorbell once; the HC runs the whole TD */
	xhci_ring_doorbell(xhci_active.slot_id, xhci_dci(0, 0));

	for (budget = XHCI_CMD_TIMEOUT_MSEC; budget > 0; budget--) {
		xhci_process_events();
		if (xfer_done)
			break;
		hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(1));
	}
	if (!xfer_done)
		USB_MSG("xHCI: control transfer timed out (bReq=0x%x)",
			req->bRequest);
}

/* setup_device: xHCI routes by slot id, so addr/ep are not needed here. */
static void
xhci_setup_device(void *priv, hcd_reg1 ep, hcd_reg1 addr,
		  hcd_datatog *out_tog, hcd_datatog *in_tog)
{
	(void)priv; (void)ep; (void)addr; (void)out_tog; (void)in_tog;
	DEBUG_DUMP;
}

/*
 * reset_device: reset the port, read the speed, then bring up the slot:
 * Enable Slot, build the input context, Address Device with BSR=1 so EP0
 * control transfers work while the USB address is still 0.
 */
static int
xhci_reset_device(void *priv, hcd_speed *speed)
{
	hcd_reg4 ps, cc;
	int i, port = xhci_active.port_idx;

	DEBUG_DUMP;
	(void)priv;

	/* Reset the port (USB3 ports may already be enabled after connect) */
	ps = HCD_RD4(xhci_op, XHCI_OP_PORTSC(port));
	HCD_WR4(xhci_op, XHCI_OP_PORTSC(port),
		(ps & ~XHCI_PORTSC_RW1CS_MASK) | XHCI_PORTSC_PR);
	for (i = 0; i < XHCI_PORT_RESET_MSEC; i++) {
		ps = HCD_RD4(xhci_op, XHCI_OP_PORTSC(port));
		if (ps & XHCI_PORTSC_PRC)
			break;
		hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(1));
	}
	/* Ack the change bits */
	HCD_WR4(xhci_op, XHCI_OP_PORTSC(port),
		(ps & ~XHCI_PORTSC_RW1CS_MASK) | XHCI_PORTSC_CHANGE_MASK);

	ps = HCD_RD4(xhci_op, XHCI_OP_PORTSC(port));
	if (!(ps & XHCI_PORTSC_CCS)) {
		USB_MSG("xHCI: no device on port %d after reset", port);
		return EXIT_FAILURE;
	}
	xhci_active.speed = XHCI_PORTSC_SPEED(ps);

	/* Translate xHCI speed id to the framework's speed */
	switch (xhci_active.speed) {
	case XHCI_SPEED_LOW:	*speed = HCD_SPEED_LOW;  break;
	case XHCI_SPEED_FULL:	*speed = HCD_SPEED_FULL; break;
	case XHCI_SPEED_HIGH:	*speed = HCD_SPEED_HIGH; break;
	default:		*speed = HCD_SPEED_HIGH; break; /* SS≈HS to fw */
	}

	/* Enable Slot */
	cc = xhci_command(0, 0, XHCI_TRB_TYPE(XHCI_TRB_TYPE_ENABLE_SLOT));
	if (cc != XHCI_TRB_CC_SUCCESS || cmd_slot == 0) {
		USB_MSG("xHCI: Enable Slot failed (cc=%u)", (unsigned)cc);
		return EXIT_FAILURE;
	}
	xhci_active.slot_id = cmd_slot;

	/* Point DCBAA[slot] at the device context, prepare EP0 ring */
	xhci_dcbaa_virt()[xhci_active.slot_id] =
		(uint64_t)xhci_device_ctx_phys();
	memset(xhci_ep0_ring_virt(), 0, XHCI_RING_TRBS * sizeof(struct xhci_trb));
	ep0_ring.enq = 0;
	ep0_ring.cycle = 1;

	/* Address Device (BSR=0): assign the address and enable EP0.  xHCI
	 * routes by slot id, so the framework's later GET_DESCRIPTOR "at
	 * address 0" still reaches this device, and its SET_ADDRESS is then
	 * a no-op (intercepted in setup_stage). */
	xhci_build_address_input();
	cc = xhci_command((hcd_reg4)xhci_input_ctx_phys(),
			  (hcd_reg4)((uint64_t)xhci_input_ctx_phys() >> 32),
			  XHCI_TRB_TYPE(XHCI_TRB_TYPE_ADDRESS_DEVICE) |
			  XHCI_TRB_SLOT(xhci_active.slot_id));
	if (cc != XHCI_TRB_CC_SUCCESS) {
		USB_MSG("xHCI: Address Device failed (cc=%u)", (unsigned)cc);
		return EXIT_FAILURE;
	}

	USB_MSG("xHCI: slot %d ready (speed id %u)",
		xhci_active.slot_id, (unsigned)xhci_active.speed);
	return EXIT_SUCCESS;
}

/*
 * setup_stage: the framework's first control-transfer stage.  Because QEMU's
 * xHCI runs a control transfer as one TD, we perform the *entire* transfer
 * here (Setup [+ Data] + Status) and then release the framework's wait.  The
 * subsequent data/status stage functions are no-ops that just release their
 * own waits.  SET_ADDRESS is a no-op transfer: the device was already
 * addressed by reset_device, and xHCI routes by slot id.
 */
static void
xhci_setup_stage(void *priv, hcd_ctrlrequest *req)
{
	DEBUG_DUMP;
	(void)priv;

	xhci_active.set_address =
		((req->bRequestType & UT_WRITE) == UT_WRITE) &&
		(req->bRequest == UR_SET_ADDRESS);

	if (xhci_active.set_address) {
		xfer_cc = XHCI_TRB_CC_SUCCESS;	/* already addressed */
		xfer_residual = 0;
		xhci_active.last_len = 0;
		xhci_data_consumed = 0;
	} else {
		xhci_control_td(req);
	}

	xhci_wake_device();	/* release the setup-stage wait */
}

/* IN data stage: the data was already transferred by setup_stage. */
static void
xhci_in_data_stage(void *priv)
{
	DEBUG_DUMP;
	(void)priv;
	xhci_wake_device();
}

/* OUT data stage: control OUT-with-data is unused during enumeration. */
static void
xhci_out_data_stage(void *priv)
{
	DEBUG_DUMP;
	(void)priv;
	xhci_wake_device();
}

/* IN status stage (no-data / OUT-data control): already done by setup_stage. */
static void
xhci_in_status_stage(void *priv)
{
	DEBUG_DUMP;
	(void)priv;
	xhci_wake_device();
}

/* OUT status stage (IN-data control): already done by setup_stage. */
static void
xhci_out_status_stage(void *priv)
{
	DEBUG_DUMP;
	(void)priv;
	xhci_wake_device();
}

/*
 * Configure a bulk endpoint (lazily, on first use): add the endpoint and its
 * transfer ring to the slot via a Configure Endpoint command.
 */
static int
xhci_configure_bulk(int ep, int is_in, unsigned max_packet)
{
	uint8_t *in = xhci_input_ctx_virt();
	struct xhci_ctx *icc  = xhci_ctx_at(in, 0);
	struct xhci_ctx *slot = xhci_ctx_at(in, 1);
	int dci = xhci_dci(ep, is_in);
	struct xhci_ctx *epc = xhci_ctx_at(in, 1 + dci);
	phys_bytes ring_phys = is_in ? xhci_bulk_in_ring_phys()
				     : xhci_bulk_out_ring_phys();
	struct xhci_trb *ring = is_in ? xhci_bulk_in_ring_virt()
				      : xhci_bulk_out_ring_virt();
	hcd_reg4 cc;

	memset(in, 0, (2 + dci) * xhci_ctx_size);

	/* Update slot (A0) so Context Entries covers this DCI, add this EP */
	icc->dw[XHCI_ICC_ADD] = XHCI_ICC_ADD_BIT(0) | XHCI_ICC_ADD_BIT(dci);

	slot->dw[0] = XHCI_SLOT_DW0_ROUTE(0) |
		      XHCI_SLOT_DW0_SPEED(xhci_active.speed) |
		      XHCI_SLOT_DW0_CTX_ENTRIES(dci);
	slot->dw[1] = XHCI_SLOT_DW1_RHPORT(xhci_active.port_idx + 1);

	epc->dw[1] = XHCI_EP_DW1_CERR(3) |
		     XHCI_EP_DW1_TYPE(is_in ? XHCI_EP_TYPE_BULK_IN
					    : XHCI_EP_TYPE_BULK_OUT) |
		     XHCI_EP_DW1_MAXPKT(max_packet);
	epc->dw[2] = (hcd_reg4)ring_phys | XHCI_EP_DCS;
	epc->dw[3] = (hcd_reg4)((uint64_t)ring_phys >> 32);
	epc->dw[4] = XHCI_EP_DW4_AVGTRB(max_packet);

	/* Fresh transfer ring */
	memset(ring, 0, XHCI_RING_TRBS * sizeof(struct xhci_trb));
	if (is_in) { bin_ring.enq = 0; bin_ring.cycle = 1; }
	else       { bout_ring.enq = 0; bout_ring.cycle = 1; }

	cc = xhci_command((hcd_reg4)xhci_input_ctx_phys(),
			  (hcd_reg4)((uint64_t)xhci_input_ctx_phys() >> 32),
			  XHCI_TRB_TYPE(XHCI_TRB_TYPE_CONFIGURE_EP) |
			  XHCI_TRB_SLOT(xhci_active.slot_id));
	if (cc != XHCI_TRB_CC_SUCCESS) {
		USB_MSG("xHCI: Configure Endpoint failed (cc=%u)",
			(unsigned)cc);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/* Drain the event ring (cooperatively) until the in-flight transfer completes */
static void
xhci_wait_xfer(void)
{
	int budget;

	for (budget = XHCI_CMD_TIMEOUT_MSEC; budget > 0; budget--) {
		xhci_process_events();
		if (xfer_done)
			return;
		hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(1));
	}
	USB_MSG("xHCI: bulk transfer timed out");
}

/* Bulk IN transfer (performed synchronously, like control) */
static void
xhci_rx_stage(void *priv, hcd_datarequest *req)
{
	int data_len, dci;

	DEBUG_DUMP;
	(void)priv;

	if (xhci_active.bin_ep != req->endpoint) {
		if (xhci_configure_bulk(req->endpoint, 1,
					req->max_packet_size) != EXIT_SUCCESS) {
			xfer_cc = 0;
			xhci_wake_device();
			return;
		}
		xhci_active.bin_ep = req->endpoint;
	}

	data_len = (req->data_left > (int)MAX_WTOTALLENGTH)
		   ? (int)MAX_WTOTALLENGTH : req->data_left;
	xhci_active.last_len = (uint32_t)data_len;
	dci = xhci_dci(req->endpoint, 1);

	xfer_is_bulk = 0;
	xfer_done = 0;
	xfer_cc = 0;
	xfer_residual = 0;
	xhci_data_consumed = 0;

	(void)xhci_ring_enqueue(xhci_bulk_in_ring_virt(),
		xhci_bulk_in_ring_phys(), &bin_ring,
		(hcd_reg4)xhci_data_buf_phys(),
		(hcd_reg4)((uint64_t)xhci_data_buf_phys() >> 32),
		XHCI_TRB_TXLEN((hcd_reg4)data_len),
		XHCI_TRB_TYPE(XHCI_TRB_TYPE_NORMAL) | XHCI_TRB_ISP |
		XHCI_TRB_IOC);
	xhci_ring_doorbell(xhci_active.slot_id, dci);

	xhci_wait_xfer();
	xhci_wake_device();
}

/* Bulk OUT transfer (performed synchronously) */
static void
xhci_tx_stage(void *priv, hcd_datarequest *req)
{
	int data_len, dci;

	DEBUG_DUMP;
	(void)priv;

	if (xhci_active.bout_ep != req->endpoint) {
		if (xhci_configure_bulk(req->endpoint, 0,
					req->max_packet_size) != EXIT_SUCCESS) {
			xfer_cc = 0;
			xhci_wake_device();
			return;
		}
		xhci_active.bout_ep = req->endpoint;
	}

	data_len = (req->data_left > (int)MAX_WTOTALLENGTH)
		   ? (int)MAX_WTOTALLENGTH : req->data_left;
	xhci_active.last_len = (uint32_t)data_len;
	dci = xhci_dci(req->endpoint, 0);

	xfer_is_bulk = 0;
	xfer_done = 0;
	xfer_cc = 0;
	xfer_residual = 0;
	xhci_data_consumed = 0;

	if (req->data != NULL && data_len > 0)
		memcpy(xhci_data_buf_virt(), req->data, (size_t)data_len);

	(void)xhci_ring_enqueue(xhci_bulk_out_ring_virt(),
		xhci_bulk_out_ring_phys(), &bout_ring,
		(hcd_reg4)xhci_data_buf_phys(),
		(hcd_reg4)((uint64_t)xhci_data_buf_phys() >> 32),
		XHCI_TRB_TXLEN((hcd_reg4)data_len),
		XHCI_TRB_TYPE(XHCI_TRB_TYPE_NORMAL) | XHCI_TRB_IOC);
	xhci_ring_doorbell(xhci_active.slot_id, dci);

	xhci_wait_xfer();
	xhci_wake_device();
}

/* Copy received data out of the DMA buffer; bytes = requested - residual */
static int
xhci_read_data(void *priv, hcd_reg1 *buf, hcd_reg1 ep)
{
	uint32_t got;

	DEBUG_DUMP;
	(void)priv; (void)ep;

	/* The whole transfer's data was delivered in one shot by setup_stage
	 * (control) or the bulk Normal TRB.  Return it once; a second call in
	 * the framework's multi-packet loop reports 0 so the loop terminates. */
	if (xhci_data_consumed)
		return 0;
	xhci_data_consumed = 1;

	got = (xfer_residual <= xhci_active.last_len)
	      ? (xhci_active.last_len - xfer_residual) : 0;

	if (got > 0 && buf != NULL)
		memcpy(buf, xhci_data_buf_virt(), got);
	return (int)got;
}

/* Inspect the last transfer event's completion code */
static int
xhci_check_error(void *priv, hcd_transfer type, hcd_reg1 ep, hcd_direction dir)
{
	DEBUG_DUMP;
	(void)priv; (void)type; (void)ep; (void)dir;

	/* Short Packet (CC 13) is success for IN transfers that returned less */
	if (xfer_cc == XHCI_TRB_CC_SUCCESS || xfer_cc == 13)
		return EXIT_SUCCESS;

	USB_MSG("xHCI: transfer error (cc=%u)", (unsigned)xfer_cc);
	return EXIT_FAILURE;
}
