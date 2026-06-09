/*
 * xHCI hardware DMA structures.
 *
 * Layouts come from the xHCI specification rev 1.2 §6.  Only the structures
 * needed for controller bring-up (Phase 4a) are defined here: the Transfer
 * Request Block (TRB) shared by all rings, and the Event Ring Segment Table
 * entry.  Device/Input Contexts and endpoint rings are added with the
 * transfer engine (Phase 4b).
 *
 * All instances reside in physically contiguous, DMA-accessible memory from
 * alloc_contig(); ring segments are 64-byte aligned (16-byte TRBs, 256 per
 * 4 KiB segment) and the segment table is 64-byte aligned per spec §6.5.
 */

#ifndef _XHCI_STRUCTS_H_
#define _XHCI_STRUCTS_H_

#include <stdint.h>

#include <usbd/hcd_common.h>	/* hcd_reg4 */
#include "xhci_regs.h"


/*===========================================================================*
 *    Transfer Request Block (TRB) — spec §6.4                              *
 *                                                                           *
 *    16 bytes.  Used by the command ring, event ring and transfer rings.    *
 *===========================================================================*/
struct xhci_trb {
	hcd_reg4 param_lo;	/* parameter / data-buffer-pointer low  */
	hcd_reg4 param_hi;	/* parameter / data-buffer-pointer high */
	hcd_reg4 status;	/* transfer length, completion code, ... */
	hcd_reg4 control;	/* cycle bit, TRB type, type-specific    */
};

typedef char _xhci_trb_size_check[(sizeof(struct xhci_trb) == 16) ? 1 : -1];

/* TRB.control fields */
#define XHCI_TRB_C		(1u << 0)	/* Cycle bit */
#define XHCI_TRB_TC		(1u << 1)	/* Toggle Cycle (Link TRB) */
#define XHCI_TRB_TYPE_SHIFT	10
#define XHCI_TRB_TYPE_MASK	(0x3Fu << XHCI_TRB_TYPE_SHIFT)
#define XHCI_TRB_TYPE(t)	(((t) & 0x3Fu) << XHCI_TRB_TYPE_SHIFT)
#define XHCI_TRB_GET_TYPE(c)	(((c) >> XHCI_TRB_TYPE_SHIFT) & 0x3Fu)

/* TRB.status fields (event TRBs) */
#define XHCI_TRB_CC_SHIFT	24
#define XHCI_TRB_GET_CC(s)	(((s) >> XHCI_TRB_CC_SHIFT) & 0xFFu)
#define XHCI_TRB_CC_SUCCESS	1

/* TRB types (spec §6.4.6) */
#define XHCI_TRB_TYPE_NORMAL		1
#define XHCI_TRB_TYPE_SETUP_STAGE	2
#define XHCI_TRB_TYPE_DATA_STAGE	3
#define XHCI_TRB_TYPE_STATUS_STAGE	4
#define XHCI_TRB_TYPE_LINK		6
#define XHCI_TRB_TYPE_ENABLE_SLOT	9
#define XHCI_TRB_TYPE_DISABLE_SLOT	10
#define XHCI_TRB_TYPE_ADDRESS_DEVICE	11
#define XHCI_TRB_TYPE_CONFIGURE_EP	12
#define XHCI_TRB_TYPE_EVALUATE_CONTEXT	13
#define XHCI_TRB_TYPE_NOOP_CMD		23
#define XHCI_TRB_TYPE_TRANSFER_EVENT	32
#define XHCI_TRB_TYPE_CMD_COMPLETION	33
#define XHCI_TRB_TYPE_PORT_STATUS_EVENT	34

/* TRB.control flags (transfer TRBs) */
#define XHCI_TRB_ENT		(1u << 1)	/* Evaluate Next TRB */
#define XHCI_TRB_ISP		(1u << 2)	/* Interrupt-on-Short-Packet */
#define XHCI_TRB_CH		(1u << 4)	/* Chain bit */
#define XHCI_TRB_IOC		(1u << 5)	/* Interrupt On Completion */
#define XHCI_TRB_IDT		(1u << 6)	/* Immediate Data */
#define XHCI_TRB_BSR		(1u << 9)	/* Block Set Address Request */
/* Setup-stage TRT (transfer type) in bits 16-17 */
#define XHCI_TRB_TRT_NODATA	(0u << 16)
#define XHCI_TRB_TRT_OUT	(2u << 16)
#define XHCI_TRB_TRT_IN		(3u << 16)
/* Data/Status-stage DIR bit (bit 16): 1 = IN */
#define XHCI_TRB_DIR_IN		(1u << 16)
/* Address Device / Configure Endpoint slot id in bits 24-31 */
#define XHCI_TRB_SLOT(s)	(((s) & 0xFFu) << 24)
#define XHCI_TRB_GET_SLOT(c)	(((c) >> 24) & 0xFFu)
/* Setup TRB transfer length is always 8 (in status dword bits 0-16) */
#define XHCI_TRB_TXLEN(n)	((n) & 0x1FFFFu)
#define XHCI_TRB_GET_TXLEN(s)	((s) & 0x1FFFFu)


/*===========================================================================*
 *    Slot / Endpoint / Input contexts — spec §6.2                          *
 *                                                                           *
 *    Each context is 32 bytes (8 dwords); when HCCPARAMS1.CSZ=1 the HC uses *
 *    a 64-byte stride (the upper 32 bytes reserved).  We define the 32-byte *
 *    field layout and stride the arrays by the runtime context size.        *
 *===========================================================================*/
struct xhci_ctx { hcd_reg4 dw[8]; };	/* generic 32-byte context */

/* Slot context dwords */
#define XHCI_SLOT_DW0_ROUTE(r)		((r) & 0xFFFFFu)
#define XHCI_SLOT_DW0_SPEED(s)		(((s) & 0xFu) << 20)
#define XHCI_SLOT_DW0_CTX_ENTRIES(n)	(((n) & 0x1Fu) << 27)
#define XHCI_SLOT_DW1_RHPORT(p)		(((p) & 0xFFu) << 16)
#define XHCI_SLOT_DW3_GET_STATE(v)	(((v) >> 27) & 0x1Fu)
#define XHCI_SLOT_DW3_GET_ADDR(v)	((v) & 0xFFu)

/* Endpoint context dwords */
#define XHCI_EP_DW0_GET_STATE(v)	((v) & 0x7u)
#define XHCI_EP_DW1_CERR(n)		(((n) & 0x3u) << 1)
#define XHCI_EP_DW1_TYPE(t)		(((t) & 0x7u) << 3)
#define XHCI_EP_DW1_MAXBURST(n)		(((n) & 0xFFu) << 8)
#define XHCI_EP_DW1_MAXPKT(n)		(((n) & 0xFFFFu) << 16)
#define XHCI_EP_DW4_AVGTRB(n)		((n) & 0xFFFFu)
/* Endpoint types */
#define XHCI_EP_TYPE_BULK_OUT		2
#define XHCI_EP_TYPE_CONTROL		4
#define XHCI_EP_TYPE_BULK_IN		6
/* Dequeue Cycle State bit lives in EP context dw2 bit0 */
#define XHCI_EP_DCS			(1u << 0)

/* Input Control Context (the first context of an Input Context) */
#define XHCI_ICC_DROP			0	/* dw0 = drop flags */
#define XHCI_ICC_ADD			1	/* dw1 = add flags */
/* Add-flag bit for context i (A0 = slot, A1 = EP0, A(n) = endpoint) */
#define XHCI_ICC_ADD_BIT(i)		(1u << (i))


/*===========================================================================*
 *    Event Ring Segment Table entry — spec §6.5                            *
 *===========================================================================*/
struct xhci_erst_entry {
	hcd_reg4 base_lo;	/* ring segment base address low (64-byte aligned) */
	hcd_reg4 base_hi;	/* ring segment base address high */
	hcd_reg4 size;		/* number of TRBs in this segment (low 16 bits) */
	hcd_reg4 reserved;
};

typedef char _xhci_erst_size_check
	[(sizeof(struct xhci_erst_entry) == 16) ? 1 : -1];


/*===========================================================================*
 *    Ring sizing                                                            *
 *===========================================================================*/
#define XHCI_RING_TRBS		64	/* TRBs per command/event ring segment */
#define XHCI_ERST_ENTRIES	1	/* single event-ring segment */


#endif /* !_XHCI_STRUCTS_H_ */
