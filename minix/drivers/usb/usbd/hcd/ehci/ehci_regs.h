/*
 * EHCI (Enhanced Host Controller Interface) register definitions.
 *
 * Based on the Intel EHCI specification, revision 1.0:
 * https://www.intel.com/content/dam/www/public/us/en/documents/
 *         technical-specifications/ehci-specification-for-usb.pdf
 *
 * EHCI capability registers start at the PCI BAR0 MMIO base address.
 * Operational registers follow at base + CAPLENGTH.
 */

#ifndef _EHCI_REGS_H_
#define _EHCI_REGS_H_


/*===========================================================================*
 *    PCI identification                                                     *
 *===========================================================================*/
/* PCI class/subclass/progif for EHCI (USB 2.0) */
#define EHCI_PCI_CLASS		0x0C	/* Serial Bus Controller */
#define EHCI_PCI_SUBCLASS	0x03	/* USB Controller */
#define EHCI_PCI_PROGIF		0x20	/* EHCI */

/* PCI register offsets used during device discovery */
#define EHCI_PCI_BAR0		0x10	/* Base Address Register 0 (MMIO) */
#define EHCI_PCI_BAR0_MMIO_MASK	(~0x0Fu)/* Mask off lower 4 flags */
#define EHCI_PCI_IRQ		0x3C	/* Interrupt Line register */

/* Minimum MMIO region size to map (capability + operational regs) */
#define EHCI_MMIO_SIZE		0x1000u


/*===========================================================================*
 *    Capability registers (at MMIO base)                                    *
 *===========================================================================*/
#define EHCI_CAPLENGTH		0x00u	/* Capability register length (8-bit) */
#define EHCI_HCIVERSION		0x02u	/* Interface version number (16-bit) */
#define EHCI_HCSPARAMS		0x04u	/* Structural parameters (32-bit) */
#define EHCI_HCCPARAMS		0x08u	/* Capability parameters (32-bit) */
#define EHCI_HCSP_PORTROUTE	0x0Cu	/* Companion port route description */

/* HCSPARAMS fields */
#define EHCI_HCS_N_PORTS(p)	((p) & 0x0Fu)	/* Number of ports */
#define EHCI_HCS_PPC		(1u << 4)	/* Port Power Control */
#define EHCI_HCS_P_INDICATOR	(1u << 16)	/* Port indicators */
#define EHCI_HCS_N_CC(p)	(((p) >> 12) & 0x0Fu)	/* Companion controllers */
#define EHCI_HCS_N_PCC(p)	(((p) >> 8)  & 0x0Fu)	/* Ports per CC */

/* HCCPARAMS fields */
#define EHCI_HCC_64BIT		(1u << 0)	/* 64-bit addressing capable */
#define EHCI_HCC_PFLF		(1u << 1)	/* Programmable frame list flag */
#define EHCI_HCC_ASPC		(1u << 2)	/* Async schedule park capability */
#define EHCI_HCC_IST(p)		(((p) >> 4) & 0x0Fu)	/* Isoch. sched. threshold */
#define EHCI_HCC_EECP(p)	(((p) >> 8) & 0xFFu)	/* Extended cap. pointer */


/*===========================================================================*
 *    Operational registers (at MMIO base + CAPLENGTH)                      *
 *===========================================================================*/
#define EHCI_USBCMD		0x00u	/* USB Command */
#define EHCI_USBSTS		0x04u	/* USB Status */
#define EHCI_USBINTR		0x08u	/* USB Interrupt Enable */
#define EHCI_FRINDEX		0x0Cu	/* USB Frame Index */
#define EHCI_CTRLDSSEGMENT	0x10u	/* 4G Segment Selector (64-bit only) */
#define EHCI_PERIODICLISTBASE	0x14u	/* Periodic Frame List Base Address */
#define EHCI_ASYNCLISTADDR	0x18u	/* Next Async List Address */
#define EHCI_CONFIGFLAG		0x40u	/* Configured Flag */
#define EHCI_PORTSC(n)		(0x44u + ((n) * 4u))	/* Port n Status/Control */

/* USBCMD fields */
#define EHCI_CMD_RS		(1u << 0)	/* Run/Stop */
#define EHCI_CMD_HCRESET	(1u << 1)	/* Host Controller Reset */
#define EHCI_CMD_FLS_1024	(0u << 2)	/* Frame list size: 1024 */
#define EHCI_CMD_FLS_512	(1u << 2)	/* Frame list size: 512  */
#define EHCI_CMD_FLS_256	(2u << 2)	/* Frame list size: 256  */
#define EHCI_CMD_PSE		(1u << 4)	/* Periodic Schedule Enable */
#define EHCI_CMD_ASE		(1u << 5)	/* Async Schedule Enable */
#define EHCI_CMD_IAAD		(1u << 6)	/* Interrupt on Async Advance Doorbell */
#define EHCI_CMD_LHCR		(1u << 7)	/* Light HC Reset */
#define EHCI_CMD_ASPMC(n)	(((n) & 0x3u) << 8)	/* Async Schedule Park Mode Count */
#define EHCI_CMD_ASPME		(1u << 11)	/* Async Schedule Park Mode Enable */
#define EHCI_CMD_ITC(n)		(((n) & 0xFFu) << 16)	/* Interrupt Threshold Control */
#define EHCI_CMD_ITC_1MF	EHCI_CMD_ITC(0x01u)	/* 1 microframe threshold */
#define EHCI_CMD_ITC_8MF	EHCI_CMD_ITC(0x08u)	/* 8 microframes (1ms) */

/* USBSTS fields */
#define EHCI_STS_USBINT		(1u << 0)	/* USB Interrupt */
#define EHCI_STS_USBERRINT	(1u << 1)	/* USB Error Interrupt */
#define EHCI_STS_PCD		(1u << 2)	/* Port Change Detect */
#define EHCI_STS_FLR		(1u << 3)	/* Frame List Rollover */
#define EHCI_STS_HSEE		(1u << 4)	/* Host System Error */
#define EHCI_STS_IAA		(1u << 5)	/* Interrupt on Async Advance */
#define EHCI_STS_HALTED		(1u << 12)	/* HC Halted */
#define EHCI_STS_RECL		(1u << 13)	/* Reclamation */
#define EHCI_STS_PSS		(1u << 14)	/* Periodic Schedule Status */
#define EHCI_STS_ASS		(1u << 15)	/* Async Schedule Status */

/* USBINTR enable bits (match USBSTS interrupt bits) */
#define EHCI_INTR_USBINT	(1u << 0)
#define EHCI_INTR_USBERRINT	(1u << 1)
#define EHCI_INTR_PCD		(1u << 2)
#define EHCI_INTR_FLR		(1u << 3)
#define EHCI_INTR_HSEE		(1u << 4)
#define EHCI_INTR_IAA		(1u << 5)

/* CONFIGFLAG fields */
#define EHCI_CF_CF		(1u << 0)	/* Configure Flag: route ports to EHCI */

/* PORTSC fields */
#define EHCI_PORT_CCS		(1u << 0)	/* Current Connect Status */
#define EHCI_PORT_CSC		(1u << 1)	/* Connect Status Change (W1C) */
#define EHCI_PORT_PE		(1u << 2)	/* Port Enable */
#define EHCI_PORT_PEC		(1u << 3)	/* Port Enable/Disable Change (W1C) */
#define EHCI_PORT_OCA		(1u << 4)	/* Over-current Active */
#define EHCI_PORT_OCC		(1u << 5)	/* Over-current Change (W1C) */
#define EHCI_PORT_FPR		(1u << 6)	/* Force Port Resume */
#define EHCI_PORT_SUSPEND	(1u << 7)	/* Suspend */
#define EHCI_PORT_PR		(1u << 8)	/* Port Reset */
#define EHCI_PORT_LS_MASK	(3u << 10)	/* Line Status */
#define EHCI_PORT_LS_SE0	(0u << 10)	/* SE0 / not connected */
#define EHCI_PORT_LS_K		(1u << 10)	/* K-state (low-speed device) */
#define EHCI_PORT_LS_J		(2u << 10)	/* J-state */
#define EHCI_PORT_PP		(1u << 12)	/* Port Power */
#define EHCI_PORT_PO		(1u << 13)	/* Port Owner (0=EHCI, 1=companion) */
#define EHCI_PORT_PIC_MASK	(3u << 14)	/* Port Indicator Control */
#define EHCI_PORT_PTC_MASK	(0xFu << 16)	/* Port Test Control */
#define EHCI_PORT_WKCNNT_E	(1u << 20)	/* Wake on Connect Enable */
#define EHCI_PORT_WKDSCNNT_E	(1u << 21)	/* Wake on Disconnect Enable */
#define EHCI_PORT_WKOC_E	(1u << 22)	/* Wake on Over-current Enable */

/* Port reset pulse width in milliseconds (USB spec: 50ms minimum) */
#define EHCI_PORT_RESET_MSEC	50u
/* Time to wait after reset before reading port status */
#define EHCI_PORT_RESET_SETTLE_MSEC	10u

/* HC reset timeout in milliseconds */
#define EHCI_RESET_TIMEOUT_MSEC		250u


/*===========================================================================*
 *    Queue Head (QH) and Transfer Descriptor (qTD) structures               *
 *                                                                           *
 *    These are placed in DMA-accessible memory and pointed to by the        *
 *    EHCI async/periodic schedule lists.  The hardware reads them           *
 *    directly to perform transfers.                                         *
 *===========================================================================*/

/* qTD token field bits */
#define EHCI_QTD_PING		(1u << 0)
#define EHCI_QTD_SPLITXSTATE	(1u << 1)
#define EHCI_QTD_MISSED_UFRAME	(1u << 2)
#define EHCI_QTD_XACT_ERR	(1u << 3)
#define EHCI_QTD_BABBLE		(1u << 4)
#define EHCI_QTD_DATA_BUFERR	(1u << 5)
#define EHCI_QTD_HALTED		(1u << 6)
#define EHCI_QTD_ACTIVE		(1u << 7)
#define EHCI_QTD_PID_OUT	(0u << 8)
#define EHCI_QTD_PID_IN		(1u << 8)
#define EHCI_QTD_PID_SETUP	(2u << 8)
#define EHCI_QTD_CERR(n)	(((n) & 0x3u) << 10)
#define EHCI_QTD_CERR_MAX	EHCI_QTD_CERR(3u)
#define EHCI_QTD_CPAGE(n)	(((n) & 0x7u) << 12)
#define EHCI_QTD_IOC		(1u << 15)	/* Interrupt on Complete */
#define EHCI_QTD_BYTES(n)	(((n) & 0x7FFFu) << 16)
#define EHCI_QTD_DT		(1u << 31)	/* Data Toggle */

#define EHCI_QTD_GET_BYTES(t)	(((t) >> 16) & 0x7FFFu)
#define EHCI_QTD_GET_STATUS(t)	((t) & 0xFFu)

/* qTD terminate bit (used in next/alt pointer fields) */
#define EHCI_PTR_TERMINATE	(1u << 0)

/* QH endpoint characteristics bits */
#define EHCI_QH_DEVADDR(n)	((n) & 0x7Fu)
#define EHCI_QH_INACTIVATE	(1u << 7)
#define EHCI_QH_ENDPT(n)	(((n) & 0xFu) << 8)
#define EHCI_QH_EPS_FULL	(0u << 12)	/* Full speed */
#define EHCI_QH_EPS_LOW		(1u << 12)	/* Low speed */
#define EHCI_QH_EPS_HIGH	(2u << 12)	/* High speed */
#define EHCI_QH_DTC		(1u << 14)	/* Data Toggle Control */
#define EHCI_QH_HEAD		(1u << 15)	/* Head of Reclamation List */
#define EHCI_QH_MAXPKT(n)	(((n) & 0x7FFu) << 16)
#define EHCI_QH_CTRLEP		(1u << 27)	/* Control Endpoint Flag */
#define EHCI_QH_NAKRL(n)	(((n) & 0xFu) << 28)

/* QH endpoint capabilities bits */
#define EHCI_QH_SMASK(n)	((n) & 0xFFu)
#define EHCI_QH_CMASK(n)	(((n) & 0xFFu) << 8)
#define EHCI_QH_HUBADDR(n)	(((n) & 0x7Fu) << 16)
#define EHCI_QH_PORT(n)		(((n) & 0x7Fu) << 23)
#define EHCI_QH_MULT(n)		(((n) & 0x3u) << 30)

/* QH horizontal link pointer type bits */
#define EHCI_LP_TYPE_ITD	(0u << 1)	/* Isochronous TD */
#define EHCI_LP_TYPE_QH		(1u << 1)	/* Queue Head */
#define EHCI_LP_TYPE_SITD	(2u << 1)	/* Split-transaction ITD */
#define EHCI_LP_TYPE_FSTN	(3u << 1)	/* Frame Span Traversal Node */


#endif /* !_EHCI_REGS_H_ */
