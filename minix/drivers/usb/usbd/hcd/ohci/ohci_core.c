/*
 * OHCI (Open Host Controller Interface) core driver.
 *
 * Implements the hcd_driver_state function table for OHCI USB 1.1 (full- and
 * low-speed) controllers found via PCI on amd64 systems.
 *
 * Architecture mirrors ehci_core.c:
 *   - ohci_pci.c   — PCI discovery and MMIO mapping
 *   - ohci_mem.c   — DMA-accessible HCCA / ED / TD / data-buffer pools
 *   - ohci_core.c  — hcd_driver_state operations (this file)
 *
 * Transfer model:
 *   One device at a time (same limitation as EHCI Phase 1 / MUSB).  A single
 *   device Endpoint Descriptor (ohci_ed_virt(OHCI_ED_DEVICE)) sits at the head
 *   of the control list and is reprogrammed in setup_device() before each
 *   operation.  Both control and bulk transfers run through the control list
 *   (the list an ED lives in only affects HC scheduling priority, not data
 *   correctness, and our transfers are fully serialised), so the bulk list is
 *   left unused.  Per stage we queue one general TD followed by a permanent
 *   dummy "tail" TD as the OHCI ED-queue protocol requires (the HC processes
 *   TDs from HeadP until HeadP == TailP).
 *
 * Sequence for a control transfer (driven by hcd.c, identical to EHCI):
 *   setup_device → setup_stage → [wait WDH ISR] → check_error
 *               → in_data_stage × N → [wait ISR] → check_error → read_data
 *               → out_status_stage → [wait ISR] → check_error
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
 *    Local constants                                                        *
 *===========================================================================*/
/* Software-reset poll: HCR self-clears within 10 us per spec §7.1.2 */
#define OHCI_RESET_TIMEOUT_USEC		100
/* Port reset assertion time (USB spec: >= 10 ms; be generous like EHCI) */
#define OHCI_PORT_RESET_MSEC		50
/* Settle time after releasing reset before the port is usable */
#define OHCI_PORT_RESET_SETTLE_MSEC	20
/* FSLargestDataPacket for the default FrameInterval (11999):
 *   FSMPS = ((FI - 210) * 6) / 7 = 0x2778 */
#define OHCI_FMI_FSMPS_DEFAULT		0x2778u


/*===========================================================================*
 *    Module-level state                                                     *
 *===========================================================================*/
static ohci_pci_device	ohci_dev;
static hcd_driver_state	ohci_driver;
static int		ohci_irq_hook = -1;

/*
 * Active transfer context.  USBD serialises all transfers through a single
 * device thread, so one concurrent transfer is the maximum.
 */
static struct {
	int		 td_idx;	/* real TD in flight, -1 = idle         */
	int		 tail_idx;	/* permanent dummy tail TD, -1 = idle   */
	uint32_t	 initial_len;	/* requested byte count for read_data   */
	hcd_reg4	 buf_phys;	/* DMA buffer phys start of the xfer    */
	hcd_datatog	*tx_tog;	/* per-device TX toggle pointer          */
	hcd_datatog	*rx_tog;	/* per-device RX toggle pointer          */
	hcd_reg1	 ep_num;	/* endpoint number of active transfer    */
	hcd_speed	 speed;		/* device speed (FULL or LOW)            */
	int		 port_idx;	/* root-hub port of the active device    */
} ohci_active;

/*
 * Completion-delivery guard + poll-backstop.  Under the extra scheduling load
 * of an attached hub (a second driver polling continuously), the DDEKit
 * cooperative scheduler can fail to deliver a WDH completion interrupt to the
 * waiting device thread, hanging the transfer.  A periodic timer polls the
 * in-flight TD's condition code and delivers the completion the ISR missed.
 * 'ohci_xfer_pending' is set when a TD is submitted and check-and-cleared by
 * whichever of the ISR / backstop delivers first, guaranteeing exactly-once
 * delivery (DDEKit threads are cooperative, so the test-and-clear is atomic).
 */
static volatile int ohci_xfer_pending;

/*
 * Deliver the in-flight transfer's completion to the waiting device thread,
 * exactly once.  Shared by the WDH ISR and the poll-backstop.
 */
static void
ohci_deliver_completion(void)
{
	hcd_reg1 ep;

	if (!ohci_xfer_pending)
		return;			/* already delivered */

	ohci_xfer_pending = 0;

	ep = (ohci_active.td_idx >= 0) ? ohci_active.ep_num : HCD_DEFAULT_EP;

	hcd_handle_event(ohci_driver.port_device[ohci_active.port_idx],
			  HCD_EVENT_ENDPOINT, ep);
}

/*
 * Poll-backstop thread.  Periodically checks whether the in-flight TD has been
 * retired by the HC (condition code no longer "not accessed") while its
 * completion is still pending delivery, and if so delivers it — recovering the
 * transfer when the WDH interrupt was not delivered to the ISR under load.
 * When a transfer is genuinely stuck waiting, the device thread is blocked, so
 * the dispatcher runs on each clock tick and wakes this thread from msleep,
 * letting it read the TD status directly and bypass the missed interrupt.
 */
static void
ohci_backstop_thread(void * UNUSED(arg))
{
	for (;;) {
		ddekit_thread_msleep(2);	/* ~2 ms poll */

		if (ohci_xfer_pending && ohci_active.td_idx >= 0) {
			struct ohci_td *td = ohci_td_virt(ohci_active.td_idx);
			hcd_reg4 cc = (td->control & OHCI_TD_CC_MASK)
				      >> OHCI_TD_CC_SHIFT;

			/* Retired by the HC?  Deliver the completion the ISR
			 * missed (no-op if the ISR already delivered it). */
			if (cc != (OHCI_TD_CC_NOT_ACCESSED >> OHCI_TD_CC_SHIFT))
				ohci_deliver_completion();
		}
	}
}


/*===========================================================================*
 *    Forward declarations                                                   *
 *===========================================================================*/
static void  ohci_setup_device    (void *, hcd_reg1, hcd_reg1,
				   hcd_datatog *, hcd_datatog *);
static int   ohci_reset_device    (void *, hcd_speed *);
static void  ohci_setup_stage     (void *, hcd_ctrlrequest *);
static void  ohci_rx_stage        (void *, hcd_datarequest *);
static void  ohci_tx_stage        (void *, hcd_datarequest *);
static void  ohci_in_data_stage   (void *);
static void  ohci_out_data_stage  (void *);
static void  ohci_in_status_stage (void *);
static void  ohci_out_status_stage(void *);
static int   ohci_read_data       (void *, hcd_reg1 *, hcd_reg1);
static int   ohci_check_error     (void *, hcd_transfer, hcd_reg1,
				   hcd_direction);

static void  ohci_isr_init(void *);
static void  ohci_isr     (void *);
static void  ohci_scan_ports(hcd_driver_state *, int);

/* One-shot: catch a device already attached when the HC went operational.
 * QEMU (and some real controllers) do not raise RHSC for a connect that was
 * present at power-on, so we scan once on the first StartOfFrame, when the
 * controller is running and the DDEKit device thread can service it. */
static int ohci_initial_scan_done;


/*===========================================================================*
 *    Internal helpers                                                       *
 *===========================================================================*/

/*
 * Software-reset the host controller (HcCommandStatus.HCR) and wait for the
 * bit to self-clear.  After this the HC is in USBSUSPEND and must be moved to
 * USBOPERATIONAL within 2 ms, so the caller programs the operational
 * registers immediately afterwards.
 */
static int
ohci_hc_reset(void)
{
	int timeout;

	HCD_WR4(ohci_dev.op_regs, OHCI_HC_CMD_STATUS, OHCI_CMD_HCR);

	timeout = OHCI_RESET_TIMEOUT_USEC;
	do {
		hcd_os_nanosleep(1000);	/* 1 us */
		if (!(HCD_RD4(ohci_dev.op_regs, OHCI_HC_CMD_STATUS)
		      & OHCI_CMD_HCR))
			break;
	} while (--timeout > 0);

	if (timeout == 0) {
		USB_MSG("OHCI: HC reset timed out");
		return EXIT_FAILURE;
	}

	USB_MSG("OHCI: HC reset complete");
	return EXIT_SUCCESS;
}

/*
 * Take ownership from SMM/BIOS if the controller reports InterruptRouting,
 * then bring the controller to USBOPERATIONAL with the control list enabled.
 */
static void
ohci_hc_start(int n_ports)
{
	struct ohci_ed *dev_ed = ohci_ed_virt(OHCI_ED_DEVICE);
	hcd_reg4 fm, ctrl, fsmps;
	int i;

	/* Save FrameInterval so we can restore FSMPS after the reset */
	fm = HCD_RD4(ohci_dev.op_regs, OHCI_HC_FM_INTERVAL);

	/* SMM hand-off: if firmware owns the HC, request ownership change */
	ctrl = HCD_RD4(ohci_dev.op_regs, OHCI_HC_CONTROL);
	if (ctrl & OHCI_CTL_IR) {
		HCD_WR4(ohci_dev.op_regs, OHCI_HC_CMD_STATUS, OHCI_CMD_OCR);
		for (i = 0; i < 1000; i++) {
			if (!(HCD_RD4(ohci_dev.op_regs, OHCI_HC_CONTROL)
			      & OHCI_CTL_IR))
				break;
			hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(1));
		}
	}

	/* Software reset (puts HC in SUSPEND; 2 ms to reach OPERATIONAL) */
	(void)ohci_hc_reset();

	/* Restore FrameInterval: keep firmware FSMPS if present, else default */
	fsmps = (fm & OHCI_FMI_FSMPS_MASK) >> OHCI_FMI_FSMPS_SHIFT;
	if (fsmps == 0)
		fsmps = OHCI_FMI_FSMPS_DEFAULT;
	HCD_WR4(ohci_dev.op_regs, OHCI_HC_FM_INTERVAL,
		OHCI_FMI_FIT |
		(fsmps << OHCI_FMI_FSMPS_SHIFT) |
		OHCI_FMI_FI_DEFAULT);
	HCD_WR4(ohci_dev.op_regs, OHCI_HC_PERIODIC_START,
		OHCI_PERIODIC_START_DEFAULT);
	HCD_WR4(ohci_dev.op_regs, OHCI_HC_LS_THRESHOLD,
		OHCI_LS_THRESHOLD_DEFAULT);

	/* The single device ED is the (only) member of the control list */
	dev_ed->control = 0;		/* skipped until setup_device() */
	dev_ed->headp   = 0;
	dev_ed->tailp   = 0;
	dev_ed->nexted  = 0;

	HCD_WR4(ohci_dev.op_regs, OHCI_HC_HCCA, (hcd_reg4)ohci_hcca_phys());
	HCD_WR4(ohci_dev.op_regs, OHCI_HC_CONTROL_HEAD_ED,
		(hcd_reg4)ohci_ed_phys(OHCI_ED_DEVICE));
	HCD_WR4(ohci_dev.op_regs, OHCI_HC_CONTROL_CUR_ED, 0);
	HCD_WR4(ohci_dev.op_regs, OHCI_HC_BULK_HEAD_ED, 0);
	HCD_WR4(ohci_dev.op_regs, OHCI_HC_BULK_CUR_ED, 0);

	/* Acknowledge any stale interrupt status, then enable the ones we use */
	HCD_WR4(ohci_dev.op_regs, OHCI_HC_INT_STATUS, OHCI_INT_ALL_EVENTS);
	HCD_WR4(ohci_dev.op_regs, OHCI_HC_INT_ENABLE,
		OHCI_INT_MIE | OHCI_INT_WDH | OHCI_INT_RHSC | OHCI_INT_UE |
		OHCI_INT_SF);	/* SF drives the one-shot initial port scan */

	/* Go operational with the control list enabled (periodic/iso/bulk off) */
	ctrl = HCD_RD4(ohci_dev.op_regs, OHCI_HC_CONTROL);
	ctrl &= ~(OHCI_CTL_HCFS_MASK | OHCI_CTL_PLE | OHCI_CTL_IE |
		  OHCI_CTL_BLE | OHCI_CTL_CBSR);
	ctrl |= OHCI_CTL_CLE | OHCI_CTL_HCFS_OPER;
	HCD_WR4(ohci_dev.op_regs, OHCI_HC_CONTROL, ctrl);

	/* Power the root-hub ports: global power on, then per-port */
	HCD_WR4(ohci_dev.op_regs, OHCI_HC_RH_STATUS, OHCI_RHS_LPSC);
	for (i = 0; i < n_ports; i++)
		HCD_WR4(ohci_dev.op_regs, OHCI_HC_RH_PORT_STATUS(i),
			OHCI_PORT_PPS);
	hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(20));
}

/*
 * Build and submit one general TD on the device ED, followed by the dummy
 * tail TD.  Common to all transfer-stage functions.
 *
 *   dp     — OHCI_TD_DP_SETUP / _IN / _OUT
 *   toggle — OHCI_TD_T_DATA0 / _DATA1
 *   data   — virtual pointer to the payload (NULL for IN / ZLP)
 *   len    — payload length in bytes
 *   is_in  — non-zero for IN transfers (enables short-packet rounding)
 *
 * Returns the real TD pool index, or -1 on failure.
 */
static int
ohci_submit_td(hcd_reg4 dp, hcd_reg4 toggle,
	       const void *data, uint32_t len, int is_in)
{
	struct ohci_ed *ed = ohci_ed_virt(OHCI_ED_DEVICE);
	struct ohci_td *td, *tail;
	int td_idx, tail_idx;

	/* Reclaim the previous stage's TDs (uniform lifecycle) */
	if (ohci_active.td_idx >= 0) {
		ohci_td_free(ohci_active.td_idx);
		ohci_active.td_idx = -1;
	}
	if (ohci_active.tail_idx >= 0) {
		ohci_td_free(ohci_active.tail_idx);
		ohci_active.tail_idx = -1;
	}

	td_idx = ohci_td_alloc();
	if (td_idx < 0)
		return -1;
	tail_idx = ohci_td_alloc();
	if (tail_idx < 0) {
		ohci_td_free(td_idx);
		return -1;
	}

	td   = ohci_td_virt(td_idx);
	tail = ohci_td_virt(tail_idx);

	/* Copy payload into the DMA buffer for OUT / SETUP */
	if (!is_in && data != NULL && len > 0)
		memcpy(ohci_xfer_buf_virt(), data, len);

	/*
	 * TD control word: CC initialised to "not accessed" (0xE) so we can
	 * tell the HC has retired it; DI=0 requests a WritebackDoneHead
	 * interrupt with no extra frame delay.
	 */
	td->control = OHCI_TD_CC_NOT_ACCESSED | dp | toggle
		      | (is_in ? OHCI_TD_R : 0u);

	if (len > 0) {
		td->cbp = (hcd_reg4)ohci_xfer_buf_phys();
		td->be  = (hcd_reg4)(ohci_xfer_buf_phys() + len - 1u);
	} else {
		td->cbp = 0;	/* zero-length packet */
		td->be  = 0;
	}
	td->nexttd = (hcd_reg4)ohci_td_phys(tail_idx);

	/* Dummy tail: the HC stops when HeadP reaches it */
	tail->control = 0;
	tail->cbp     = 0;
	tail->nexttd  = 0;
	tail->be      = 0;

	/* Link into the device ED (H=0 clears any prior halt; C=0) */
	ed->headp = (hcd_reg4)ohci_td_phys(td_idx);
	ed->tailp = (hcd_reg4)ohci_td_phys(tail_idx);

	ohci_active.td_idx      = td_idx;
	ohci_active.tail_idx    = tail_idx;
	ohci_active.initial_len = len;
	ohci_active.buf_phys    = (len > 0) ? (hcd_reg4)ohci_xfer_buf_phys() : 0;

	/* A TD is now in flight; arm completion delivery (ISR or backstop) */
	ohci_xfer_pending = 1;

	/* Tell the HC the control list has work */
	HCD_WR4(ohci_dev.op_regs, OHCI_HC_CMD_STATUS, OHCI_CMD_CLF);

	return td_idx;
}


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
	memset(&ohci_active, 0, sizeof(ohci_active));
	ohci_active.td_idx   = -1;
	ohci_active.tail_idx = -1;
	ohci_initial_scan_done = 0;

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
	rev     = HCD_RD4(ohci_dev.op_regs, OHCI_HC_REVISION);
	rhda    = HCD_RD4(ohci_dev.op_regs, OHCI_HC_RH_DESC_A);
	n_ports = (int)OHCI_RHDA_NDP(rhda);
	if (n_ports < 1)
		n_ports = 1;
	if (n_ports > HCD_MAX_PORTS)
		n_ports = HCD_MAX_PORTS;

	USB_MSG("OHCI: HcRevision=0x%02x%s, %d root-hub port(s)",
		(unsigned)(rev & OHCI_REV_MASK),
		(rev & OHCI_REV_LEGACY) ? " (legacy emul)" : "",
		n_ports);

	/* 5. Register IRQ */
	ohci_irq_hook = hcd_os_interrupt_attach(ohci_dev.irq,
						 ohci_isr_init,
						 ohci_isr,
						 &ohci_driver);
	if (ohci_irq_hook != EXIT_SUCCESS) {
		USB_MSG("OHCI: failed to attach IRQ %d", ohci_dev.irq);
		ohci_mem_deinit();
		ohci_pci_unmap(&ohci_dev);
		return EXIT_FAILURE;
	}

	/* 6. Wire hcd_driver_state operations table */
	ohci_driver.controller_id    = 1;	/* EHCI claims controller_id 0 */
	ohci_driver.setup_device     = ohci_setup_device;
	ohci_driver.reset_device     = ohci_reset_device;
	ohci_driver.setup_stage      = ohci_setup_stage;
	ohci_driver.rx_stage         = ohci_rx_stage;
	ohci_driver.tx_stage         = ohci_tx_stage;
	ohci_driver.in_data_stage    = ohci_in_data_stage;
	ohci_driver.out_data_stage   = ohci_out_data_stage;
	ohci_driver.in_status_stage  = ohci_in_status_stage;
	ohci_driver.out_status_stage = ohci_out_status_stage;
	ohci_driver.read_data        = ohci_read_data;
	ohci_driver.check_error      = ohci_check_error;
	ohci_driver.private_data     = &ohci_dev;

	/* 7. Bring the controller to USBOPERATIONAL and power the ports */
	ohci_hc_start(n_ports);

	/* 8. Enable delivery of the controller IRQ */
	hcd_os_interrupt_enable(ohci_dev.irq);

	/* 9. Start the completion poll-backstop (recovers missed WDH IRQs
	 * under the scheduling load of an attached hub) */
	(void)ddekit_thread_create(ohci_backstop_thread, NULL, "ohci_backstop");

	USB_MSG("OHCI: controller initialized (irq %d)", ohci_dev.irq);
	return EXIT_SUCCESS;
}


/*===========================================================================*
 *    ohci_deinit                                                            *
 *===========================================================================*/
void
ohci_deinit(void)
{
	hcd_reg4 ctrl;

	DEBUG_DUMP;

	if (ohci_dev.op_regs != NULL) {
		/* Disable interrupts and stop the lists */
		HCD_WR4(ohci_dev.op_regs, OHCI_HC_INT_DISABLE,
			OHCI_INT_MIE);
		ctrl = HCD_RD4(ohci_dev.op_regs, OHCI_HC_CONTROL);
		ctrl &= ~(OHCI_CTL_CLE | OHCI_CTL_BLE | OHCI_CTL_PLE |
			  OHCI_CTL_IE);
		HCD_WR4(ohci_dev.op_regs, OHCI_HC_CONTROL, ctrl);
	}

	if (ohci_irq_hook == EXIT_SUCCESS) {
		hcd_os_interrupt_disable(ohci_dev.irq);
		hcd_os_interrupt_detach(ohci_dev.irq);
		ohci_irq_hook = -1;
	}

	ohci_mem_deinit();
	ohci_pci_unmap(&ohci_dev);
}


/*===========================================================================*
 *    ISR callbacks                                                          *
 *===========================================================================*/
static void
ohci_isr_init(void *priv)
{
	(void)priv;	/* nothing extra beyond hcd_os_interrupt_attach */
}

/*
 * Scan every root-hub port for connect/disconnect changes and deliver the
 * corresponding event.  Shared by the RHSC interrupt and the one-shot
 * StartOfFrame initial scan.  If force is set, a present device is reported
 * even when its connect-status-change bit is not set (used for the initial
 * scan of devices already attached at power-on).
 */
static void
ohci_scan_ports(hcd_driver_state *drv, int force)
{
	hcd_reg4 rhda = HCD_RD4(ohci_dev.op_regs, OHCI_HC_RH_DESC_A);
	int n_ports = (int)OHCI_RHDA_NDP(rhda);
	int i;

	if (n_ports > HCD_MAX_PORTS)
		n_ports = HCD_MAX_PORTS;

	for (i = 0; i < n_ports; i++) {
		hcd_reg4 ps = HCD_RD4(ohci_dev.op_regs,
				       OHCI_HC_RH_PORT_STATUS(i));

		if (!(ps & OHCI_PORT_CSC) && !(force && (ps & OHCI_PORT_CCS)))
			continue;

		/* Clear connect-status-change (W1C) */
		HCD_WR4(ohci_dev.op_regs, OHCI_HC_RH_PORT_STATUS(i),
			OHCI_PORT_CSC);

		if (ps & OHCI_PORT_CCS) {
			USB_MSG("OHCI: device connected on port %d", i);
			hcd_update_port(drv, HCD_EVENT_CONNECTED, i);
			ohci_active.port_idx = i;
			hcd_handle_event(drv->port_device[i],
					  HCD_EVENT_CONNECTED, 0);
		} else {
			USB_MSG("OHCI: device disconnected from port %d", i);
			if (ohci_active.td_idx >= 0) {
				ohci_td_free(ohci_active.td_idx);
				ohci_active.td_idx = -1;
			}
			if (ohci_active.tail_idx >= 0) {
				ohci_td_free(ohci_active.tail_idx);
				ohci_active.tail_idx = -1;
			}
			hcd_handle_event(drv->port_device[i],
					  HCD_EVENT_DISCONNECTED, 0);
			hcd_update_port(drv, HCD_EVENT_DISCONNECTED, i);
		}
	}
}

static void
ohci_isr(void *priv)
{
	hcd_driver_state *drv = (hcd_driver_state *)priv;
	hcd_reg4 ints;

	ints = HCD_RD4(ohci_dev.op_regs, OHCI_HC_INT_STATUS);
	ints &= HCD_RD4(ohci_dev.op_regs, OHCI_HC_INT_ENABLE);

	if (ints == 0)
		return;		/* not ours / spurious */

	if (ints & OHCI_INT_UE)
		USB_MSG("OHCI: unrecoverable HC error");

	/*
	 * One-shot initial scan on the first StartOfFrame: a device attached
	 * before the HC went operational does not generate RHSC, so we look
	 * for it here, then disable the SF interrupt we no longer need.
	 */
	if ((ints & OHCI_INT_SF) && !ohci_initial_scan_done) {
		ohci_initial_scan_done = 1;
		HCD_WR4(ohci_dev.op_regs, OHCI_HC_INT_DISABLE, OHCI_INT_SF);
		ohci_scan_ports(drv, 1 /* force present devices */);
	}

	if (ints & OHCI_INT_RHSC)
		ohci_scan_ports(drv, 0);

	if (ints & OHCI_INT_WDH) {
		/*
		 * One or more TDs have been retired to the done queue.  We
		 * track the single in-flight TD directly, so we only need to
		 * wake the device thread; check_error() inspects the TD's
		 * condition code.  Delivered exactly once (see ohci_xfer_pending).
		 */
		ohci_deliver_completion();
	}

	/* Acknowledge everything we handled (W1C).  Clearing WDH lets the HC
	 * rebuild HccaDoneHead for the next completion. */
	HCD_WR4(ohci_dev.op_regs, OHCI_HC_INT_STATUS, ints);
}


/*===========================================================================*
 *    hcd_driver_state operations                                            *
 *===========================================================================*/

/*---------------------------------------------------------------------------*
 *    ohci_setup_device                                                      *
 *                                                                           *
 *    Program the device ED for the given (addr, ep) pair.  Direction is     *
 *    taken from each TD (ED.D = 00), so the same ED serves control and bulk.*
 *---------------------------------------------------------------------------*/
static void
ohci_setup_device(void *priv, hcd_reg1 ep, hcd_reg1 addr,
		  hcd_datatog *out_tog, hcd_datatog *in_tog)
{
	struct ohci_ed *ed = ohci_ed_virt(OHCI_ED_DEVICE);

	DEBUG_DUMP;
	(void)priv;

	ed->control =
		((hcd_reg4)addr & OHCI_ED_FA_MASK) |
		(((hcd_reg4)ep << OHCI_ED_EN_SHIFT) & OHCI_ED_EN_MASK) |
		OHCI_ED_D_FROM_TD |
		((ohci_active.speed == HCD_SPEED_LOW) ? OHCI_ED_S : 0u) |
		(((hcd_reg4)HCD_HS_MAXPACKETSIZE << OHCI_ED_MPS_SHIFT)
		 & OHCI_ED_MPS_MASK);

	ed->headp  = 0;		/* empty queue until a stage submits a TD */
	ed->tailp  = 0;
	ed->nexted = 0;

	ohci_active.ep_num = ep;
	ohci_active.tx_tog = out_tog;
	ohci_active.rx_tog = in_tog;

	/*
	 * Route this transfer's completion (WDH) interrupt to the device whose
	 * transfer we are programming.  The generic layer set active_port just
	 * before calling us; transfers are serialised, so this is unambiguous.
	 */
	ohci_active.port_idx = ohci_driver.active_port;

	USB_MSG("OHCI: setup_device addr=%u ep=%u port=%d", addr, ep,
		ohci_active.port_idx);
}

/*---------------------------------------------------------------------------*
 *    ohci_reset_device                                                      *
 *                                                                           *
 *    Assert port reset on port 0, release it, then read the port status to  *
 *    determine full- vs low-speed.                                          *
 *---------------------------------------------------------------------------*/
static int
ohci_reset_device(void *priv, hcd_speed *speed)
{
	hcd_reg4 ps;
	int i, port;

	DEBUG_DUMP;
	(void)priv;

	/* Which root-hub port to reset (set by the generic layer) */
	port = ohci_driver.enum_port;

	/* Assert reset on the device's port */
	HCD_WR4(ohci_dev.op_regs, OHCI_HC_RH_PORT_STATUS(port), OHCI_PORT_PRS);

	hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(OHCI_PORT_RESET_MSEC));

	/* Wait for the reset to complete (PRSC set by HC) */
	for (i = 0; i < OHCI_PORT_RESET_MSEC; i++) {
		ps = HCD_RD4(ohci_dev.op_regs, OHCI_HC_RH_PORT_STATUS(port));
		if (ps & OHCI_PORT_PRSC)
			break;
		hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(1));
	}

	/* Clear reset-status-change (W1C) */
	HCD_WR4(ohci_dev.op_regs, OHCI_HC_RH_PORT_STATUS(port), OHCI_PORT_PRSC);

	hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(OHCI_PORT_RESET_SETTLE_MSEC));

	ps = HCD_RD4(ohci_dev.op_regs, OHCI_HC_RH_PORT_STATUS(port));

	if (!(ps & OHCI_PORT_CCS)) {
		USB_MSG("OHCI: no device present after reset on port %d", port);
		return EXIT_FAILURE;
	}

	if (ps & OHCI_PORT_LSDA) {
		*speed = HCD_SPEED_LOW;
		ohci_active.speed = HCD_SPEED_LOW;
		USB_MSG("OHCI: low-speed device on port %d", port);
	} else {
		*speed = HCD_SPEED_FULL;
		ohci_active.speed = HCD_SPEED_FULL;
		USB_MSG("OHCI: full-speed device on port %d", port);
	}

	return EXIT_SUCCESS;
}

/*---------------------------------------------------------------------------*
 *    ohci_setup_stage — SETUP token of a control transfer (DATA0)           *
 *---------------------------------------------------------------------------*/
static void
ohci_setup_stage(void *priv, hcd_ctrlrequest *req)
{
	DEBUG_DUMP;
	(void)priv;

	if (ohci_submit_td(OHCI_TD_DP_SETUP, OHCI_TD_T_DATA0,
			   req, sizeof(*req), 0) < 0)
		USB_MSG("OHCI: setup_stage: TD alloc failed");
}

/*---------------------------------------------------------------------------*
 *    ohci_in_data_stage — IN data phase of a control transfer (DATA1)       *
 *---------------------------------------------------------------------------*/
static void
ohci_in_data_stage(void *priv)
{
	DEBUG_DUMP;
	(void)priv;

	if (ohci_submit_td(OHCI_TD_DP_IN, OHCI_TD_T_DATA1,
			   NULL, MAX_WTOTALLENGTH, 1) < 0)
		USB_MSG("OHCI: in_data_stage: TD alloc failed");
}

/*---------------------------------------------------------------------------*
 *    ohci_out_data_stage — OUT data phase of a control transfer (DATA1)     *
 *---------------------------------------------------------------------------*/
static void
ohci_out_data_stage(void *priv)
{
	DEBUG_DUMP;
	(void)priv;

	/* The OUT bytes were placed in the xfer buffer by a prior submit. */
	if (ohci_submit_td(OHCI_TD_DP_OUT, OHCI_TD_T_DATA1,
			   NULL, MAX_WTOTALLENGTH, 0) < 0)
		USB_MSG("OHCI: out_data_stage: TD alloc failed");
}

/*---------------------------------------------------------------------------*
 *    ohci_in_status_stage — zero-length IN status (DATA1)                   *
 *---------------------------------------------------------------------------*/
static void
ohci_in_status_stage(void *priv)
{
	DEBUG_DUMP;
	(void)priv;

	if (ohci_submit_td(OHCI_TD_DP_IN, OHCI_TD_T_DATA1, NULL, 0, 1) < 0)
		USB_MSG("OHCI: in_status_stage: TD alloc failed");
}

/*---------------------------------------------------------------------------*
 *    ohci_out_status_stage — zero-length OUT status (DATA1)                 *
 *---------------------------------------------------------------------------*/
static void
ohci_out_status_stage(void *priv)
{
	DEBUG_DUMP;
	(void)priv;

	if (ohci_submit_td(OHCI_TD_DP_OUT, OHCI_TD_T_DATA1, NULL, 0, 0) < 0)
		USB_MSG("OHCI: out_status_stage: TD alloc failed");
}

/*---------------------------------------------------------------------------*
 *    ohci_rx_stage — bulk / interrupt IN transfer                           *
 *---------------------------------------------------------------------------*/
static void
ohci_rx_stage(void *priv, hcd_datarequest *req)
{
	struct ohci_ed *ed = ohci_ed_virt(OHCI_ED_DEVICE);
	hcd_reg4 toggle;
	int data_len;

	DEBUG_DUMP;
	(void)priv;

	/* Update ED max-packet from the endpoint descriptor */
	ed->control = (ed->control & ~OHCI_ED_MPS_MASK)
		      | (((hcd_reg4)req->max_packet_size << OHCI_ED_MPS_SHIFT)
			 & OHCI_ED_MPS_MASK);

	data_len = (req->data_left > (int)MAX_WTOTALLENGTH)
		   ? (int)MAX_WTOTALLENGTH
		   : req->data_left;

	toggle = (ohci_active.rx_tog != NULL &&
		  *ohci_active.rx_tog == HCD_DATATOG_DATA1)
		 ? OHCI_TD_T_DATA1 : OHCI_TD_T_DATA0;

	if (ohci_submit_td(OHCI_TD_DP_IN, toggle, NULL,
			   (uint32_t)data_len, 1) < 0)
		USB_MSG("OHCI: rx_stage: TD alloc failed");
}

/*---------------------------------------------------------------------------*
 *    ohci_tx_stage — bulk / interrupt OUT transfer                          *
 *---------------------------------------------------------------------------*/
static void
ohci_tx_stage(void *priv, hcd_datarequest *req)
{
	struct ohci_ed *ed = ohci_ed_virt(OHCI_ED_DEVICE);
	hcd_reg4 toggle;
	int data_len;

	DEBUG_DUMP;
	(void)priv;

	ed->control = (ed->control & ~OHCI_ED_MPS_MASK)
		      | (((hcd_reg4)req->max_packet_size << OHCI_ED_MPS_SHIFT)
			 & OHCI_ED_MPS_MASK);

	data_len = (req->data_left > (int)MAX_WTOTALLENGTH)
		   ? (int)MAX_WTOTALLENGTH
		   : req->data_left;

	toggle = (ohci_active.tx_tog != NULL &&
		  *ohci_active.tx_tog == HCD_DATATOG_DATA1)
		 ? OHCI_TD_T_DATA1 : OHCI_TD_T_DATA0;

	if (ohci_submit_td(OHCI_TD_DP_OUT, toggle, req->data,
			   (uint32_t)data_len, 0) < 0)
		USB_MSG("OHCI: tx_stage: TD alloc failed");
}

/*---------------------------------------------------------------------------*
 *    ohci_read_data                                                         *
 *                                                                           *
 *    Copy data received by the HC from the DMA buffer into the caller's     *
 *    buffer.  Bytes transferred is derived from the retired TD's CBP:       *
 *    CBP == 0 means the whole buffer was used; otherwise CBP points at the  *
 *    next (untouched) byte.                                                 *
 *---------------------------------------------------------------------------*/
static int
ohci_read_data(void *priv, hcd_reg1 *buf, hcd_reg1 ep)
{
	struct ohci_td *td;
	uint32_t bytes_xferred;

	DEBUG_DUMP;
	(void)priv; (void)ep;

	if (ohci_active.td_idx < 0) {
		USB_MSG("OHCI: read_data called with no active TD");
		return HCD_READ_ERR;
	}

	td = ohci_td_virt(ohci_active.td_idx);

	if (td->cbp == 0)
		bytes_xferred = ohci_active.initial_len;
	else
		bytes_xferred = td->cbp - ohci_active.buf_phys;

	if (bytes_xferred > ohci_active.initial_len)
		bytes_xferred = ohci_active.initial_len;	/* paranoia */

	if (bytes_xferred > 0)
		memcpy(buf, ohci_xfer_buf_virt(), bytes_xferred);

	/* Leave the TD allocated; the next ohci_submit_td() reclaims it. */
	return (int)bytes_xferred;
}

/*---------------------------------------------------------------------------*
 *    ohci_check_error                                                       *
 *                                                                           *
 *    Inspect the retired TD's ConditionCode.  On a successful bulk/int      *
 *    transfer advance the relevant data toggle.                            *
 *---------------------------------------------------------------------------*/
static int
ohci_check_error(void *priv, hcd_transfer type, hcd_reg1 ep,
		 hcd_direction dir)
{
	struct ohci_td *td;
	hcd_reg4 cc;

	DEBUG_DUMP;
	(void)priv; (void)type;

	if (ohci_active.td_idx < 0) {
		USB_MSG("OHCI: check_error called with no active TD");
		return EXIT_FAILURE;
	}

	td = ohci_td_virt(ohci_active.td_idx);
	cc = (td->control & OHCI_TD_CC_MASK) >> OHCI_TD_CC_SHIFT;

	/* Still not accessed by the HC?  Should not happen after WDH. */
	if (cc == (OHCI_TD_CC_NOT_ACCESSED >> OHCI_TD_CC_SHIFT)) {
		USB_MSG("OHCI: check_error: TD not retired (control=0x%08x)",
			td->control);
		return EXIT_FAILURE;
	}

	if (cc != (OHCI_TD_CC_NOERROR >> OHCI_TD_CC_SHIFT)) {
		USB_MSG("OHCI: transfer error (cc=0x%x ep=%u dir=%u)",
			(unsigned)cc, ep, dir);
		ohci_td_free(ohci_active.td_idx);
		ohci_active.td_idx = -1;
		if (ohci_active.tail_idx >= 0) {
			ohci_td_free(ohci_active.tail_idx);
			ohci_active.tail_idx = -1;
		}
		return EXIT_FAILURE;
	}

	/* Success: advance data toggle for bulk/interrupt endpoints.  Control
	 * EP0 toggles are set explicitly per stage, so no flip is needed. */
	if (ep != HCD_DEFAULT_EP) {
		if (dir == HCD_DIRECTION_OUT && ohci_active.tx_tog != NULL)
			*ohci_active.tx_tog ^= HCD_DATATOG_DATA1;
		else if (dir == HCD_DIRECTION_IN && ohci_active.rx_tog != NULL)
			*ohci_active.rx_tog ^= HCD_DATATOG_DATA1;
	}

	return EXIT_SUCCESS;
}
