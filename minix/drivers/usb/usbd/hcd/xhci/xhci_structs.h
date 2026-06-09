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

/* TRB types we care about for bring-up */
#define XHCI_TRB_TYPE_LINK		6
#define XHCI_TRB_TYPE_NOOP_CMD		23
#define XHCI_TRB_TYPE_ENABLE_SLOT	9
#define XHCI_TRB_TYPE_TRANSFER_EVENT	32
#define XHCI_TRB_TYPE_CMD_COMPLETION	33
#define XHCI_TRB_TYPE_PORT_STATUS_EVENT	34


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
