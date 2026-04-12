/*
 * EHCI (Enhanced Host Controller Interface) core driver — Phase 1.
 *
 * Implements the hcd_driver_state function table for EHCI USB 2.0 controllers
 * found via PCI on amd64 systems.
 *
 * Architecture mirrors musb_am335x / musb_core for ARM:
 *   - ehci_pci.c   — PCI discovery and MMIO mapping
 *   - ehci_mem.c   — DMA-accessible QH / qTD / data-buffer pool
 *   - ehci_core.c  — hcd_driver_state operations (this file)
 *
 * Transfer model (Phase 1):
 *   One device at a time (same limitation as MUSB).  A single device QH
 *   (ehci_qh_virt(EHCI_QH_DEVICE)) is reused for every transfer; its
 *   endpoint characteristics are reprogrammed in setup_device() before each
 *   operation.  qTDs are drawn from the small pool in ehci_mem.c and freed
 *   after each completed transfer.
 *
 * Sequence for a control transfer (mirroring hcd.c → musb_core.c):
 *   setup_device  → setup_stage → [wait ISR] → check_error
 *                → in_data_stage × N → [wait ISR] → check_error → read_data
 *                → out_status_stage → [wait ISR] → check_error
 *
 * References:
 *   Intel EHCI Specification for USB, rev 1.0
 *   USB 2.0 Specification §8 (transfer types, data toggles)
 */

#include <stdlib.h>
#include <string.h>

#include <minix/drivers.h>

#include <usbd/hcd_common.h>
#include <usbd/hcd_interface.h>
#include <usbd/usbd_common.h>

#include "ehci_core.h"
#include "ehci_mem.h"
#include "ehci_pci.h"
#include "ehci_regs.h"
#include "ehci_structs.h"


/*===========================================================================*
 *    Module-level state                                                     *
 *===========================================================================*/
static ehci_pci_device	ehci_dev;
static hcd_driver_state	ehci_driver;
static int		ehci_irq_hook = -1;

/*
 * Active transfer context.
 * Because the USBD architecture serialises all transfers through a single
 * device thread, one concurrent transfer is the maximum.
 */
static struct {
	int		 qh_idx;	/* QH in use (EHCI_QH_DEVICE normally)  */
	int		 qtd_idx;	/* qTD currently in flight, -1 = idle   */
	uint32_t	 initial_len;	/* requested byte count for read_data   */
	hcd_datatog	*tx_tog;	/* per-device TX toggle pointer          */
	hcd_datatog	*rx_tog;	/* per-device RX toggle pointer          */
	hcd_reg1	 ep_num;	/* endpoint number of active transfer    */
	hcd_speed	 speed;		/* device speed (always HIGH in Phase 1) */
} ehci_active;


/*===========================================================================*
 *    Forward declarations                                                   *
 *===========================================================================*/
static void  ehci_setup_device    (void *, hcd_reg1, hcd_reg1,
				   hcd_datatog *, hcd_datatog *);
static int   ehci_reset_device    (void *, hcd_speed *);
static void  ehci_setup_stage     (void *, hcd_ctrlrequest *);
static void  ehci_rx_stage        (void *, hcd_datarequest *);
static void  ehci_tx_stage        (void *, hcd_datarequest *);
static void  ehci_in_data_stage   (void *);
static void  ehci_out_data_stage  (void *);
static void  ehci_in_status_stage (void *);
static void  ehci_out_status_stage(void *);
static int   ehci_read_data       (void *, hcd_reg1 *, hcd_reg1);
static int   ehci_check_error     (void *, hcd_transfer, hcd_reg1,
				   hcd_direction);

static void  ehci_isr_init(void *);
static void  ehci_isr     (void *);


/*===========================================================================*
 *    Internal helpers                                                       *
 *===========================================================================*/

/*
 * Reset the host controller and wait for HCRESET to self-clear.
 */
static int
ehci_hc_reset(void)
{
	hcd_reg4 cmd;
	int timeout;

	cmd = HCD_RD4(ehci_dev.op_regs, EHCI_USBCMD);
	if (cmd & EHCI_CMD_RS) {
		HCD_CLR(cmd, EHCI_CMD_RS);
		HCD_WR4(ehci_dev.op_regs, EHCI_USBCMD, cmd);

		timeout = EHCI_RESET_TIMEOUT_MSEC;
		do {
			hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(1));
			if (HCD_RD4(ehci_dev.op_regs, EHCI_USBSTS)
			    & EHCI_STS_HALTED)
				break;
		} while (--timeout > 0);

		if (timeout == 0) {
			USB_MSG("EHCI: HC did not halt in %dms",
				EHCI_RESET_TIMEOUT_MSEC);
			return EXIT_FAILURE;
		}
	}

	cmd = HCD_RD4(ehci_dev.op_regs, EHCI_USBCMD);
	HCD_SET(cmd, EHCI_CMD_HCRESET);
	HCD_WR4(ehci_dev.op_regs, EHCI_USBCMD, cmd);

	timeout = EHCI_RESET_TIMEOUT_MSEC;
	do {
		hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(1));
		if (!(HCD_RD4(ehci_dev.op_regs, EHCI_USBCMD) & EHCI_CMD_HCRESET))
			break;
	} while (--timeout > 0);

	if (timeout == 0) {
		USB_MSG("EHCI: HC reset timed out after %dms",
			EHCI_RESET_TIMEOUT_MSEC);
		return EXIT_FAILURE;
	}

	USB_MSG("EHCI: HC reset complete");
	return EXIT_SUCCESS;
}

/*
 * Initialise the async schedule ring with a self-pointing sentinel QH,
 * route all ports to EHCI, and start the controller.
 */
static void
ehci_hc_start(void)
{
	struct ehci_qh *sentinel = ehci_qh_virt(EHCI_QH_SENTINEL);
	hcd_reg4 cmd;

	/*
	 * Sentinel QH: circular self-link, HEAD-of-reclamation-list flag,
	 * high-speed, max-packet 64, NAK-reload = 15.
	 * No active qTD.
	 */
	sentinel->horiz_link  = (hcd_reg4)(ehci_qh_phys(EHCI_QH_SENTINEL)
				| EHCI_LP_TYPE_QH);
	sentinel->endpoint    = EHCI_QH_HEAD | EHCI_QH_EPS_HIGH
				| EHCI_QH_MAXPKT(64) | EHCI_QH_NAKRL(0xF);
	sentinel->endpoint2   = EHCI_QH_MULT(1);
	sentinel->current_qtd = EHCI_PTR_TERMINATE;
	sentinel->overlay.next     = EHCI_PTR_TERMINATE;
	sentinel->overlay.alt_next = EHCI_PTR_TERMINATE;
	sentinel->overlay.token    = 0;

	/* Point ASYNCLISTADDR at the sentinel */
	HCD_WR4(ehci_dev.op_regs, EHCI_ASYNCLISTADDR,
		(hcd_reg4)ehci_qh_phys(EHCI_QH_SENTINEL));

	/* Route all ports to EHCI (away from companion controllers) */
	HCD_WR4(ehci_dev.op_regs, EHCI_CONFIGFLAG, EHCI_CF_CF);

	/* Enable: USB transfer complete, USB error, port change, host error */
	HCD_WR4(ehci_dev.op_regs, EHCI_USBINTR,
		EHCI_INTR_USBINT | EHCI_INTR_USBERRINT |
		EHCI_INTR_PCD   | EHCI_INTR_HSEE);

	/* Start HC, enable async schedule, set 8-microframe interrupt threshold */
	cmd = HCD_RD4(ehci_dev.op_regs, EHCI_USBCMD);
	HCD_SET(cmd, EHCI_CMD_RS | EHCI_CMD_ASE);
	cmd |= EHCI_CMD_ITC_8MF;
	HCD_WR4(ehci_dev.op_regs, EHCI_USBCMD, cmd);
}

/*
 * Link the device QH into the async ring immediately after the sentinel.
 * Safe to call while the HC is running: the 32-bit pointer write is atomic
 * at the hardware level per the EHCI spec.
 */
static void
ehci_async_link_device_qh(void)
{
	struct ehci_qh *sentinel   = ehci_qh_virt(EHCI_QH_SENTINEL);
	struct ehci_qh *device_qh  = ehci_qh_virt(EHCI_QH_DEVICE);

	/* device_qh → sentinel (inherit sentinel's current next) */
	device_qh->horiz_link = sentinel->horiz_link;

	/* sentinel → device_qh */
	sentinel->horiz_link = (hcd_reg4)(ehci_qh_phys(EHCI_QH_DEVICE)
				| EHCI_LP_TYPE_QH);
}

/*
 * Remove the device QH from the async ring by reconnecting the sentinel
 * directly to whatever was after the device QH.
 */
static void
ehci_async_unlink_device_qh(void)
{
	struct ehci_qh *sentinel  = ehci_qh_virt(EHCI_QH_SENTINEL);
	struct ehci_qh *device_qh = ehci_qh_virt(EHCI_QH_DEVICE);

	sentinel->horiz_link = device_qh->horiz_link;
}

/*
 * Build and submit a single qTD.  Common to all transfer stage functions.
 *
 *  qh_idx    — QH pool index to link into
 *  token     — fully assembled qTD token word (EHCI_QTD_* bits)
 *  data      — virtual pointer to the payload (may be NULL for ZLP)
 *  data_len  — payload length in bytes
 *
 * Returns the qTD pool index on success, -1 on failure.
 */
static int
ehci_submit_qtd(int qh_idx, hcd_reg4 token,
		const void *data, uint32_t data_len)
{
	struct ehci_qtd *qtd;
	struct ehci_qh  *qh;
	int qtd_idx;

	qtd_idx = ehci_qtd_alloc();
	if (qtd_idx < 0)
		return -1;

	qtd = ehci_qtd_virt(qtd_idx);
	qh  = ehci_qh_virt(qh_idx);

	/* Copy payload into the DMA transfer buffer if provided */
	if (data != NULL && data_len > 0)
		memcpy(ehci_xfer_buf_virt(), data, data_len);

	/* Populate qTD fields */
	qtd->next     = EHCI_PTR_TERMINATE;
	qtd->alt_next = EHCI_PTR_TERMINATE;
	qtd->token    = token;

	/*
	 * Single-page transfer: buf[0] holds the physical start address.
	 * MAX_WTOTALLENGTH (1024 B) fits in one 4 KB page so buf[1..4]
	 * are left zeroed.
	 */
	qtd->buf[0] = (data_len > 0)
		      ? (hcd_reg4)ehci_xfer_buf_phys()
		      : 0;

	/*
	 * Link qTD into QH overlay.  The HC picks it up on the next
	 * traversal of the async ring.  The overlay.token is zeroed so
	 * the HC sees an inactive overlay and advances to qtd->next.
	 */
	qh->overlay.next  = (hcd_reg4)ehci_qtd_phys(qtd_idx);
	qh->overlay.token = 0;

	/* Record for check_error / read_data */
	ehci_active.qtd_idx     = qtd_idx;
	ehci_active.initial_len = data_len;

	return qtd_idx;
}


/*===========================================================================*
 *    ehci_init                                                              *
 *===========================================================================*/
int
ehci_init(void)
{
	hcd_reg4 hcsparams;
	int n_ports, i;

	DEBUG_DUMP;

	memset(&ehci_dev,    0, sizeof(ehci_dev));
	memset(&ehci_driver, 0, sizeof(ehci_driver));
	memset(&ehci_active, 0, sizeof(ehci_active));
	ehci_active.qtd_idx = -1;

	/* 1. Find EHCI controller on PCI bus */
	if (ehci_pci_find(&ehci_dev) != EXIT_SUCCESS)
		return EXIT_FAILURE;

	/* 2. Map MMIO registers */
	if (ehci_pci_map(&ehci_dev) != EXIT_SUCCESS)
		return EXIT_FAILURE;

	/* 3. Allocate DMA memory pool */
	if (ehci_mem_init() != EXIT_SUCCESS) {
		ehci_pci_unmap(&ehci_dev);
		return EXIT_FAILURE;
	}

	/* 4. Log capability info */
	hcsparams = HCD_RD4(ehci_dev.cap_regs, EHCI_HCSPARAMS);
	n_ports   = (int)EHCI_HCS_N_PORTS(hcsparams);
	USB_MSG("EHCI: %d root-hub port(s), %d companion controller(s)",
		n_ports, (int)EHCI_HCS_N_CC(hcsparams));

	/* 5. Reset the host controller */
	if (ehci_hc_reset() != EXIT_SUCCESS) {
		ehci_mem_deinit();
		ehci_pci_unmap(&ehci_dev);
		return EXIT_FAILURE;
	}

	/* 6. Register IRQ */
	ehci_irq_hook = hcd_os_interrupt_attach(ehci_dev.irq,
						 ehci_isr_init,
						 ehci_isr,
						 &ehci_driver);
	if (ehci_irq_hook < 0) {
		USB_MSG("EHCI: failed to attach IRQ %d", ehci_dev.irq);
		ehci_mem_deinit();
		ehci_pci_unmap(&ehci_dev);
		return EXIT_FAILURE;
	}

	/* 7. Wire hcd_driver_state operations table */
	ehci_driver.setup_device     = ehci_setup_device;
	ehci_driver.reset_device     = ehci_reset_device;
	ehci_driver.setup_stage      = ehci_setup_stage;
	ehci_driver.rx_stage         = ehci_rx_stage;
	ehci_driver.tx_stage         = ehci_tx_stage;
	ehci_driver.in_data_stage    = ehci_in_data_stage;
	ehci_driver.out_data_stage   = ehci_out_data_stage;
	ehci_driver.in_status_stage  = ehci_in_status_stage;
	ehci_driver.out_status_stage = ehci_out_status_stage;
	ehci_driver.read_data        = ehci_read_data;
	ehci_driver.check_error      = ehci_check_error;
	ehci_driver.private_data     = &ehci_dev;

	/* 8. Power on ports (if the controller supports per-port power) */
	hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(20));
	if (hcsparams & EHCI_HCS_PPC) {
		for (i = 0; i < n_ports; i++) {
			hcd_reg4 portsc = HCD_RD4(ehci_dev.op_regs,
						   EHCI_PORTSC(i));
			HCD_SET(portsc, EHCI_PORT_PP);
			HCD_WR4(ehci_dev.op_regs, EHCI_PORTSC(i), portsc);
		}
		hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(20));
	}

	/* 9. Build async ring and start HC */
	ehci_hc_start();

	hcd_os_interrupt_enable(ehci_irq_hook);

	USB_MSG("EHCI: controller initialized");
	return EXIT_SUCCESS;
}


/*===========================================================================*
 *    ehci_deinit                                                            *
 *===========================================================================*/
void
ehci_deinit(void)
{
	hcd_reg4 cmd;

	DEBUG_DUMP;

	if (ehci_dev.op_regs != NULL) {
		/* Disable async schedule, then stop HC */
		cmd = HCD_RD4(ehci_dev.op_regs, EHCI_USBCMD);
		HCD_CLR(cmd, EHCI_CMD_RS | EHCI_CMD_ASE);
		HCD_WR4(ehci_dev.op_regs, EHCI_USBCMD, cmd);
	}

	if (ehci_irq_hook >= 0) {
		hcd_os_interrupt_disable(ehci_irq_hook);
		hcd_os_interrupt_detach(ehci_irq_hook);
		ehci_irq_hook = -1;
	}

	ehci_mem_deinit();
	ehci_pci_unmap(&ehci_dev);
}


/*===========================================================================*
 *    ISR callbacks                                                          *
 *===========================================================================*/
static void
ehci_isr_init(void *priv)
{
	(void)priv;	/* nothing extra needed beyond hcd_os_interrupt_attach */
}

static void
ehci_isr(void *priv)
{
	hcd_driver_state *drv = (hcd_driver_state *)priv;
	hcd_reg4 sts, hcsparams;
	int n_ports, i;

	sts = HCD_RD4(ehci_dev.op_regs, EHCI_USBSTS);

	/* Acknowledge all pending interrupt bits (W1C) */
	HCD_WR4(ehci_dev.op_regs, EHCI_USBSTS, sts);

	if (sts & EHCI_STS_HSEE)
		USB_MSG("EHCI: host system error (USBSTS=0x%08x)", sts);

	if (sts & EHCI_STS_PCD) {
		/*
		 * Scan every port for connect/disconnect changes.
		 * Phase 1: all events are routed to the single port_device.
		 */
		hcsparams = HCD_RD4(ehci_dev.cap_regs, EHCI_HCSPARAMS);
		n_ports   = (int)EHCI_HCS_N_PORTS(hcsparams);

		for (i = 0; i < n_ports; i++) {
			hcd_reg4 portsc = HCD_RD4(ehci_dev.op_regs,
						   EHCI_PORTSC(i));

			if (!(portsc & EHCI_PORT_CSC))
				continue;

			/* Clear connect-status-change (W1C) */
			HCD_WR4(ehci_dev.op_regs, EHCI_PORTSC(i), portsc);

			if (portsc & EHCI_PORT_CCS) {
				USB_MSG("EHCI: device connected on port %d", i);
				hcd_handle_event(drv->port_device,
						  HCD_EVENT_CONNECTED, 0);
			} else {
				USB_MSG("EHCI: device disconnected from port %d", i);
				if (ehci_active.qtd_idx >= 0) {
					ehci_qtd_free(ehci_active.qtd_idx);
					ehci_active.qtd_idx = -1;
				}
				ehci_async_unlink_device_qh();
				hcd_handle_event(drv->port_device,
						  HCD_EVENT_DISCONNECTED, 0);
			}
		}
	}

	if (sts & (EHCI_STS_USBINT | EHCI_STS_USBERRINT)) {
		/*
		 * A qTD completed (or errored).  Wake the device thread with
		 * the endpoint number that was active at submission time.
		 * check_error() will inspect the qTD token for the actual status.
		 */
		hcd_reg1 ep = (ehci_active.qtd_idx >= 0)
			      ? ehci_active.ep_num
			      : HCD_DEFAULT_EP;

		hcd_handle_event(drv->port_device, HCD_EVENT_ENDPOINT, ep);
	}
}


/*===========================================================================*
 *    hcd_driver_state operations                                            *
 *===========================================================================*/

/*---------------------------------------------------------------------------*
 *    ehci_setup_device                                                      *
 *                                                                           *
 *    Program the device QH for the given (addr, ep) pair and store the     *
 *    data-toggle pointers for later use by check_error.                    *
 *---------------------------------------------------------------------------*/
static void
ehci_setup_device(void *priv, hcd_reg1 addr, hcd_reg1 ep,
		  hcd_datatog *out_tog, hcd_datatog *in_tog)
{
	struct ehci_qh *qh = ehci_qh_virt(EHCI_QH_DEVICE);

	DEBUG_DUMP;
	(void)priv;

	/*
	 * Endpoint characteristics word:
	 *   DTC=1  — QH owns data toggle for control transfers on EP0
	 *   EPS    — always HIGH in Phase 1 (Phase 3 adds FS/LS via OHCI)
	 *   MAXPKT — 64 bytes for HS EP0; bulk EPs updated in rx/tx_stage
	 *   NAKRL  — 15 (maximum NAK reload count)
	 */
	qh->endpoint = EHCI_QH_DEVADDR(addr)
		       | EHCI_QH_ENDPT(ep)
		       | EHCI_QH_EPS_HIGH
		       | EHCI_QH_DTC
		       | EHCI_QH_MAXPKT(HCD_HS_MAXPACKETSIZE)
		       | EHCI_QH_NAKRL(0xF);

	qh->endpoint2     = EHCI_QH_MULT(1);
	qh->current_qtd   = EHCI_PTR_TERMINATE;
	qh->overlay.next  = EHCI_PTR_TERMINATE;
	qh->overlay.alt_next = EHCI_PTR_TERMINATE;
	qh->overlay.token = 0;

	/* Link device QH into async ring (sentinel → device_qh → sentinel) */
	ehci_async_link_device_qh();

	/* Record context for this transfer sequence */
	ehci_active.qh_idx  = EHCI_QH_DEVICE;
	ehci_active.ep_num  = ep;
	ehci_active.tx_tog  = out_tog;
	ehci_active.rx_tog  = in_tog;

	USB_MSG("EHCI: setup_device addr=%u ep=%u", addr, ep);
}

/*---------------------------------------------------------------------------*
 *    ehci_reset_device                                                      *
 *                                                                           *
 *    Assert port reset, release it, then read PORTSC to determine speed.   *
 *    High-speed (PE=1): stay on EHCI.                                      *
 *    Full/low-speed (PE=0): release to companion controller via PO=1.      *
 *    Companion hand-off is not yet implemented (Phase 3).                  *
 *---------------------------------------------------------------------------*/
static int
ehci_reset_device(void *priv, hcd_speed *speed)
{
	hcd_reg4 portsc;

	DEBUG_DUMP;
	(void)priv;

	/* Assert reset on port 0 (Phase 1: single port) */
	portsc = HCD_RD4(ehci_dev.op_regs, EHCI_PORTSC(0));
	HCD_CLR(portsc, EHCI_PORT_PE);
	HCD_SET(portsc, EHCI_PORT_PR);
	HCD_WR4(ehci_dev.op_regs, EHCI_PORTSC(0), portsc);

	hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(EHCI_PORT_RESET_MSEC));

	/* Release reset */
	portsc = HCD_RD4(ehci_dev.op_regs, EHCI_PORTSC(0));
	HCD_CLR(portsc, EHCI_PORT_PR);
	HCD_WR4(ehci_dev.op_regs, EHCI_PORTSC(0), portsc);

	hcd_os_nanosleep(HCD_NANOSLEEP_MSEC(EHCI_PORT_RESET_SETTLE_MSEC));

	portsc = HCD_RD4(ehci_dev.op_regs, EHCI_PORTSC(0));

	if (portsc & EHCI_PORT_PE) {
		*speed = HCD_SPEED_HIGH;
		ehci_active.speed = HCD_SPEED_HIGH;
		USB_MSG("EHCI: high-speed device on port 0");
		return EXIT_SUCCESS;
	}

	/* Full/low-speed device: release port to companion controller */
	HCD_SET(portsc, EHCI_PORT_PO);
	HCD_WR4(ehci_dev.op_regs, EHCI_PORTSC(0), portsc);
	USB_MSG("EHCI: FS/LS device on port 0; "
		"companion hand-off not implemented (Phase 3)");
	return EXIT_FAILURE;
}

/*---------------------------------------------------------------------------*
 *    ehci_setup_stage                                                       *
 *                                                                           *
 *    Issue the SETUP token of a control transfer.                           *
 *    Builds one qTD with PID_SETUP, DATA0, 8-byte payload.                *
 *---------------------------------------------------------------------------*/
static void
ehci_setup_stage(void *priv, hcd_ctrlrequest *req)
{
	hcd_reg4 token;

	DEBUG_DUMP;
	(void)priv;

	/*
	 * SETUP always uses DATA0 (DT=0).  IOC causes an interrupt when
	 * the HC has received the ACK handshake.
	 */
	token = EHCI_QTD_PID_SETUP
		| EHCI_QTD_CERR_MAX
		| EHCI_QTD_BYTES(sizeof(*req))
		| EHCI_QTD_IOC
		| EHCI_QTD_ACTIVE;
	/* DT bit = 0 (DATA0) — left clear */

	if (ehci_submit_qtd(ehci_active.qh_idx, token, req, sizeof(*req)) < 0)
		USB_MSG("EHCI: setup_stage: qTD alloc failed");
}

/*---------------------------------------------------------------------------*
 *    ehci_in_data_stage                                                     *
 *                                                                           *
 *    IN data phase of a control transfer.  Submits a PID_IN qTD with       *
 *    DATA1 and enough buffer for up to MAX_WTOTALLENGTH bytes.             *
 *---------------------------------------------------------------------------*/
static void
ehci_in_data_stage(void *priv)
{
	hcd_reg4 token;

	DEBUG_DUMP;
	(void)priv;

	/*
	 * DATA1 for the first IN data packet of a control transfer.
	 * The HC (with DTC=1) manages the toggle for subsequent packets.
	 */
	token = EHCI_QTD_PID_IN
		| EHCI_QTD_DT		/* DATA1 */
		| EHCI_QTD_CERR_MAX
		| EHCI_QTD_BYTES(MAX_WTOTALLENGTH)
		| EHCI_QTD_IOC
		| EHCI_QTD_ACTIVE;

	/* buf[0] is the receive target — no source data to copy */
	if (ehci_submit_qtd(ehci_active.qh_idx, token, NULL, MAX_WTOTALLENGTH) < 0)
		USB_MSG("EHCI: in_data_stage: qTD alloc failed");
}

/*---------------------------------------------------------------------------*
 *    ehci_out_data_stage                                                    *
 *                                                                           *
 *    OUT data phase of a control transfer.  The caller's data is in        *
 *    device->control_data; we receive it via the generic HCD layer which   *
 *    will call tx_stage with a datarequest pointing to that buffer.        *
 *    This function issues the PID_OUT qTD directly using the xfer_buf.    *
 *---------------------------------------------------------------------------*/
static void
ehci_out_data_stage(void *priv)
{
	hcd_reg4 token;

	DEBUG_DUMP;
	(void)priv;

	/*
	 * The OUT data bytes were placed into ehci_xfer_buf by a prior
	 * memcpy in ehci_submit_qtd.  We don't know the length here —
	 * hcd.c drives this through tx_stage for bulk; for control OUT
	 * this path is rare and not yet tested.  Use MAX_WTOTALLENGTH
	 * as a safe upper bound; the HC stops at the actual ZLP.
	 */
	token = EHCI_QTD_PID_OUT
		| EHCI_QTD_DT		/* DATA1 */
		| EHCI_QTD_CERR_MAX
		| EHCI_QTD_BYTES(MAX_WTOTALLENGTH)
		| EHCI_QTD_IOC
		| EHCI_QTD_ACTIVE;

	if (ehci_submit_qtd(ehci_active.qh_idx, token, NULL, MAX_WTOTALLENGTH) < 0)
		USB_MSG("EHCI: out_data_stage: qTD alloc failed");
}

/*---------------------------------------------------------------------------*
 *    ehci_in_status_stage                                                   *
 *                                                                           *
 *    IN status phase (zero-length IN packet) after an OUT data stage.      *
 *---------------------------------------------------------------------------*/
static void
ehci_in_status_stage(void *priv)
{
	hcd_reg4 token;

	DEBUG_DUMP;
	(void)priv;

	token = EHCI_QTD_PID_IN
		| EHCI_QTD_DT		/* DATA1 */
		| EHCI_QTD_CERR_MAX
		| EHCI_QTD_BYTES(0)	/* zero-length */
		| EHCI_QTD_IOC
		| EHCI_QTD_ACTIVE;

	if (ehci_submit_qtd(ehci_active.qh_idx, token, NULL, 0) < 0)
		USB_MSG("EHCI: in_status_stage: qTD alloc failed");
}

/*---------------------------------------------------------------------------*
 *    ehci_out_status_stage                                                  *
 *                                                                           *
 *    OUT status phase (zero-length OUT packet) after an IN data stage.     *
 *---------------------------------------------------------------------------*/
static void
ehci_out_status_stage(void *priv)
{
	hcd_reg4 token;

	DEBUG_DUMP;
	(void)priv;

	token = EHCI_QTD_PID_OUT
		| EHCI_QTD_DT		/* DATA1 */
		| EHCI_QTD_CERR_MAX
		| EHCI_QTD_BYTES(0)	/* zero-length */
		| EHCI_QTD_IOC
		| EHCI_QTD_ACTIVE;

	if (ehci_submit_qtd(ehci_active.qh_idx, token, NULL, 0) < 0)
		USB_MSG("EHCI: out_status_stage: qTD alloc failed");
}

/*---------------------------------------------------------------------------*
 *    ehci_rx_stage                                                          *
 *                                                                           *
 *    Bulk / interrupt IN transfer.  Updates the device QH's max-packet     *
 *    size from the endpoint descriptor and submits a PID_IN qTD.           *
 *---------------------------------------------------------------------------*/
static void
ehci_rx_stage(void *priv, hcd_datarequest *req)
{
	struct ehci_qh *qh = ehci_qh_virt(EHCI_QH_DEVICE);
	hcd_reg4 token;
	int data_len;

	DEBUG_DUMP;
	(void)priv;

	/* Update QH max-packet from the endpoint descriptor */
	qh->endpoint = (qh->endpoint
			& ~EHCI_QH_MAXPKT(0x7FFu))
		       | EHCI_QH_MAXPKT(req->max_packet_size);

	/* Clamp to what fits in the DMA buffer */
	data_len = (req->data_left > (int)MAX_WTOTALLENGTH)
		   ? (int)MAX_WTOTALLENGTH
		   : req->data_left;

	token = EHCI_QTD_PID_IN
		| EHCI_QTD_CERR_MAX
		| EHCI_QTD_BYTES((hcd_reg4)data_len)
		| EHCI_QTD_IOC
		| EHCI_QTD_ACTIVE;

	/* Apply RX data toggle */
	if (ehci_active.rx_tog != NULL && *ehci_active.rx_tog == HCD_DATATOG_DATA1)
		token |= EHCI_QTD_DT;

	if (ehci_submit_qtd(ehci_active.qh_idx, token, NULL, (uint32_t)data_len) < 0)
		USB_MSG("EHCI: rx_stage: qTD alloc failed");
}

/*---------------------------------------------------------------------------*
 *    ehci_tx_stage                                                          *
 *                                                                           *
 *    Bulk / interrupt OUT transfer.  Copies req->data into the DMA         *
 *    buffer and submits a PID_OUT qTD.                                     *
 *---------------------------------------------------------------------------*/
static void
ehci_tx_stage(void *priv, hcd_datarequest *req)
{
	struct ehci_qh *qh = ehci_qh_virt(EHCI_QH_DEVICE);
	hcd_reg4 token;
	int data_len;

	DEBUG_DUMP;
	(void)priv;

	qh->endpoint = (qh->endpoint
			& ~EHCI_QH_MAXPKT(0x7FFu))
		       | EHCI_QH_MAXPKT(req->max_packet_size);

	data_len = (req->data_left > (int)MAX_WTOTALLENGTH)
		   ? (int)MAX_WTOTALLENGTH
		   : req->data_left;

	token = EHCI_QTD_PID_OUT
		| EHCI_QTD_CERR_MAX
		| EHCI_QTD_BYTES((hcd_reg4)data_len)
		| EHCI_QTD_IOC
		| EHCI_QTD_ACTIVE;

	if (ehci_active.tx_tog != NULL && *ehci_active.tx_tog == HCD_DATATOG_DATA1)
		token |= EHCI_QTD_DT;

	/* req->data is a virtual pointer; ehci_submit_qtd copies it to xfer_buf */
	if (ehci_submit_qtd(ehci_active.qh_idx, token,
			    req->data, (uint32_t)data_len) < 0)
		USB_MSG("EHCI: tx_stage: qTD alloc failed");
}

/*---------------------------------------------------------------------------*
 *    ehci_read_data                                                         *
 *                                                                           *
 *    Copy data received by the HC from the DMA buffer into the caller's    *
 *    buffer.  Returns number of bytes received, or HCD_READ_ERR.           *
 *                                                                           *
 *    Called by hcd.c after a successful check_error on an IN transfer.     *
 *---------------------------------------------------------------------------*/
static int
ehci_read_data(void *priv, hcd_reg1 *buf, hcd_reg1 ep)
{
	struct ehci_qtd *qtd;
	uint32_t bytes_left, bytes_xferred;

	DEBUG_DUMP;
	(void)priv; (void)ep;

	if (ehci_active.qtd_idx < 0) {
		USB_MSG("EHCI: read_data called with no active qTD");
		return HCD_READ_ERR;
	}

	qtd = ehci_qtd_virt(ehci_active.qtd_idx);

	/*
	 * bytes_left in token = bytes NOT yet transferred.
	 * bytes_xferred = what we initially requested minus what's left.
	 */
	bytes_left    = EHCI_QTD_GET_BYTES(qtd->token);
	bytes_xferred = ehci_active.initial_len - bytes_left;

	if (bytes_xferred > 0)
		memcpy(buf, ehci_xfer_buf_virt(), bytes_xferred);

	ehci_qtd_free(ehci_active.qtd_idx);
	ehci_active.qtd_idx = -1;

	return (int)bytes_xferred;
}

/*---------------------------------------------------------------------------*
 *    ehci_check_error                                                       *
 *                                                                           *
 *    Inspect the completed qTD's token field for error bits.               *
 *    On a successful TX: advance the TX data toggle.                       *
 *    On a successful RX: advance the RX data toggle.                       *
 *    On error: free the qTD and return EXIT_FAILURE.                       *
 *---------------------------------------------------------------------------*/
static int
ehci_check_error(void *priv, hcd_transfer type, hcd_reg1 ep,
		 hcd_direction dir)
{
	struct ehci_qtd *qtd;
	hcd_reg4 token;

	DEBUG_DUMP;
	(void)priv; (void)type; (void)ep;

	if (ehci_active.qtd_idx < 0) {
		USB_MSG("EHCI: check_error called with no active qTD");
		return EXIT_FAILURE;
	}

	qtd   = ehci_qtd_virt(ehci_active.qtd_idx);
	token = qtd->token;

	/* Still active? Should not happen (ISR fired), but be defensive */
	if (token & EHCI_QTD_ACTIVE) {
		USB_MSG("EHCI: check_error: qTD still active (token=0x%08x)",
			token);
		return EXIT_FAILURE;
	}

	/* Any error bit set? */
	if (token & (EHCI_QTD_HALTED | EHCI_QTD_XACT_ERR |
		     EHCI_QTD_BABBLE  | EHCI_QTD_DATA_BUFERR)) {
		USB_MSG("EHCI: transfer error (token=0x%08x ep=%u dir=%u)",
			token, ep, dir);
		ehci_qtd_free(ehci_active.qtd_idx);
		ehci_active.qtd_idx = -1;
		return EXIT_FAILURE;
	}

	/*
	 * Successful transfer: update data toggle for bulk/interrupt EPs.
	 * Control EP0 toggle is managed by the HC (DTC=1 in QH); no manual
	 * flip is needed.
	 */
	if (ep != HCD_DEFAULT_EP) {
		if (dir == HCD_DIRECTION_OUT && ehci_active.tx_tog != NULL)
			*ehci_active.tx_tog ^= HCD_DATATOG_DATA1;
		else if (dir == HCD_DIRECTION_IN && ehci_active.rx_tog != NULL)
			*ehci_active.rx_tog ^= HCD_DATATOG_DATA1;
	}

	/*
	 * For TX (OUT) transfers, free the qTD here since read_data is not
	 * called for OUT directions.  For RX (IN) transfers, read_data
	 * will free the qTD after copying the data.
	 */
	if (dir == HCD_DIRECTION_OUT || type == HCD_TRANSFER_CONTROL) {
		if (dir != HCD_DIRECTION_IN) {
			ehci_qtd_free(ehci_active.qtd_idx);
			ehci_active.qtd_idx = -1;
		}
	}

	return EXIT_SUCCESS;
}
