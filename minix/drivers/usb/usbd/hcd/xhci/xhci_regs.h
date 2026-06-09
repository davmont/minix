/*
 * xHCI (Extensible Host Controller Interface) register definitions.
 *
 * Based on the eXtensible Host Controller Interface for Universal Serial Bus
 * (xHCI) Requirements Specification, revision 1.2 (Intel, 2019).
 *
 * Unlike EHCI/OHCI, xHCI exposes four distinct register windows, all within
 * the single PCI BAR0 MMIO region:
 *
 *   Capability registers   — at MMIO base; CAPLENGTH gives the offset to...
 *   Operational registers  — at base + CAPLENGTH
 *   Runtime registers      — at base + RTSOFF (interrupter / event-ring regs)
 *   Doorbell array         — at base + DBOFF
 *
 * Only the subset needed for controller bring-up and port detection is
 * defined here; the transfer engine (Phase 4b) will add the rest.
 */

#ifndef _XHCI_REGS_H_
#define _XHCI_REGS_H_


/*===========================================================================*
 *    PCI identification                                                     *
 *===========================================================================*/
#define XHCI_PCI_CLASS		0x0C	/* Serial Bus Controller */
#define XHCI_PCI_SUBCLASS	0x03	/* USB Controller */
#define XHCI_PCI_PROGIF		0x30	/* xHCI */

#define XHCI_PCI_BAR0		0x10	/* Base Address Register 0 (MMIO) */
#define XHCI_PCI_BAR0_MMIO_MASK	(~0x0Fu)
#define XHCI_PCI_IRQ		0x3C	/* Interrupt Line register */

/* xHCI register files are large; map a generous window (64 KiB). */
#define XHCI_MMIO_SIZE		0x10000u


/*===========================================================================*
 *    Capability registers (at MMIO base)                                    *
 *===========================================================================*/
#define XHCI_CAP_CAPLENGTH	0x00u	/* 8b CAPLENGTH | 8b rsvd | 16b HCIVERSION */
#define XHCI_CAP_HCSPARAMS1	0x04u	/* MaxSlots / MaxIntrs / MaxPorts */
#define XHCI_CAP_HCSPARAMS2	0x08u	/* ERST max, scratchpad bufs */
#define XHCI_CAP_HCSPARAMS3	0x0Cu
#define XHCI_CAP_HCCPARAMS1	0x10u	/* capability params (CSZ, xECP, ...) */
#define XHCI_CAP_DBOFF		0x14u	/* Doorbell array offset */
#define XHCI_CAP_RTSOFF		0x18u	/* Runtime register space offset */
#define XHCI_CAP_HCCPARAMS2	0x1Cu

#define XHCI_CAPLENGTH(v)	((v) & 0xFFu)
#define XHCI_HCIVERSION(v)	(((v) >> 16) & 0xFFFFu)

#define XHCI_HCS1_MAXSLOTS(v)	((v) & 0xFFu)
#define XHCI_HCS1_MAXINTRS(v)	(((v) >> 8) & 0x7FFu)
#define XHCI_HCS1_MAXPORTS(v)	(((v) >> 24) & 0xFFu)

#define XHCI_HCS2_MAX_SCRATCHPAD(v)					\
	(((((v) >> 21) & 0x1Fu) << 5) | (((v) >> 27) & 0x1Fu))

#define XHCI_HCC1_CSZ		(1u << 2)	/* Context Size: 1=64B, 0=32B */
#define XHCI_HCC1_XECP(v)	(((v) >> 16) & 0xFFFFu)	/* ext-cap ptr (dwords) */

#define XHCI_DBOFF_MASK		(~0x3u)
#define XHCI_RTSOFF_MASK	(~0x1Fu)


/*===========================================================================*
 *    Operational registers (at MMIO base + CAPLENGTH)                       *
 *===========================================================================*/
#define XHCI_OP_USBCMD		0x00u	/* USB Command */
#define XHCI_OP_USBSTS		0x04u	/* USB Status */
#define XHCI_OP_PAGESIZE	0x08u	/* Page Size */
#define XHCI_OP_DNCTRL		0x14u	/* Device Notification Control */
#define XHCI_OP_CRCR		0x18u	/* Command Ring Control (64-bit) */
#define XHCI_OP_DCBAAP		0x30u	/* Device Context Base Addr Array Ptr (64b) */
#define XHCI_OP_CONFIG		0x38u	/* Configure (MaxSlotsEn) */
/* Port register set starts at 0x400; each port is 16 bytes (4 dwords). */
#define XHCI_OP_PORTSC(p)	(0x400u + 0x10u * (unsigned)(p))
#define XHCI_OP_PORTPMSC(p)	(0x404u + 0x10u * (unsigned)(p))
#define XHCI_OP_PORTLI(p)	(0x408u + 0x10u * (unsigned)(p))

/* USBCMD bits */
#define XHCI_CMD_RS		(1u << 0)	/* Run/Stop */
#define XHCI_CMD_HCRST		(1u << 1)	/* Host Controller Reset */
#define XHCI_CMD_INTE		(1u << 2)	/* Interrupter Enable */
#define XHCI_CMD_HSEE		(1u << 3)	/* Host System Error Enable */

/* USBSTS bits */
#define XHCI_STS_HCH		(1u << 0)	/* HCHalted */
#define XHCI_STS_HSE		(1u << 2)	/* Host System Error */
#define XHCI_STS_EINT		(1u << 3)	/* Event Interrupt */
#define XHCI_STS_PCD		(1u << 4)	/* Port Change Detect */
#define XHCI_STS_CNR		(1u << 11)	/* Controller Not Ready */

/* CRCR bits (low dword) */
#define XHCI_CRCR_RCS		(1u << 0)	/* Ring Cycle State */
#define XHCI_CRCR_CS		(1u << 1)	/* Command Stop */
#define XHCI_CRCR_CA		(1u << 2)	/* Command Abort */
#define XHCI_CRCR_CRR		(1u << 3)	/* Command Ring Running */

/* PORTSC bits */
#define XHCI_PORTSC_CCS		(1u << 0)	/* Current Connect Status */
#define XHCI_PORTSC_PED		(1u << 1)	/* Port Enabled/Disabled */
#define XHCI_PORTSC_OCA		(1u << 3)	/* Over-current Active */
#define XHCI_PORTSC_PR		(1u << 4)	/* Port Reset */
#define XHCI_PORTSC_PP		(1u << 9)	/* Port Power */
#define XHCI_PORTSC_SPEED(v)	(((v) >> 10) & 0xFu)	/* Port Speed */
#define XHCI_PORTSC_CSC		(1u << 17)	/* Connect Status Change */
#define XHCI_PORTSC_PEC		(1u << 18)	/* Port Enable Change */
#define XHCI_PORTSC_PRC		(1u << 21)	/* Port Reset Change */
/* Change bits are RW1CS; mask to ack without disturbing PED/PP. */
#define XHCI_PORTSC_CHANGE_MASK	(XHCI_PORTSC_CSC | XHCI_PORTSC_PEC |	\
				 XHCI_PORTSC_PRC)
/* Bits that are write-1-to-clear or otherwise must not be re-asserted when
 * doing a read-modify-write of PORTSC. */
#define XHCI_PORTSC_RW1CS_MASK	(XHCI_PORTSC_PED | (0xFFu << 17))

/* Port speed IDs (default xHCI PSI mapping) */
#define XHCI_SPEED_FULL		1
#define XHCI_SPEED_LOW		2
#define XHCI_SPEED_HIGH		3
#define XHCI_SPEED_SUPER	4


/*===========================================================================*
 *    Runtime registers (at MMIO base + RTSOFF)                              *
 *                                                                           *
 *    Interrupter 0 register set begins at RTSOFF + 0x20; each interrupter   *
 *    is 32 bytes.                                                           *
 *===========================================================================*/
#define XHCI_RT_MFINDEX		0x00u	/* Microframe Index */
#define XHCI_RT_IR0		0x20u	/* Interrupter Register Set 0 base */
#define XHCI_IR_IMAN		0x00u	/* Interrupter Management */
#define XHCI_IR_IMOD		0x04u	/* Interrupter Moderation */
#define XHCI_IR_ERSTSZ		0x08u	/* Event Ring Segment Table Size */
#define XHCI_IR_ERSTBA		0x10u	/* Event Ring Segment Table Base Addr (64b) */
#define XHCI_IR_ERDP		0x18u	/* Event Ring Dequeue Pointer (64b) */

#define XHCI_IMAN_IP		(1u << 0)	/* Interrupt Pending (RW1C) */
#define XHCI_IMAN_IE		(1u << 1)	/* Interrupt Enable */
#define XHCI_ERDP_EHB		(1u << 3)	/* Event Handler Busy (RW1C) */


/*===========================================================================*
 *    Doorbell array (at MMIO base + DBOFF)                                   *
 *                                                                           *
 *    One 32-bit doorbell per device slot; index 0 is the command doorbell.  *
 *===========================================================================*/
#define XHCI_DB(slot)		(4u * (unsigned)(slot))
#define XHCI_DB_HOST		0	/* slot 0 = command ring doorbell */
#define XHCI_DB_TARGET_CMD	0	/* DB target for the command ring */


/*===========================================================================*
 *    Extended capabilities (USB legacy support hand-off)                    *
 *===========================================================================*/
#define XHCI_ECAP_ID(v)		((v) & 0xFFu)
#define XHCI_ECAP_NEXT(v)	(((v) >> 8) & 0xFFu)	/* in dwords */
#define XHCI_ECAP_ID_LEGACY	1u
#define XHCI_LEGSUP_BIOS_OWNED	(1u << 16)
#define XHCI_LEGSUP_OS_OWNED	(1u << 24)


#endif /* !_XHCI_REGS_H_ */
