/*
 * OHCI hardware descriptor structures.
 *
 * These map directly onto the memory layouts the OHCI host controller
 * DMA-reads and DMA-writes.  Sizes and alignments come from the OpenHCI
 * specification rev 1.0a §§4.2–4.4.
 *
 * All instances must reside in physically contiguous, DMA-accessible memory
 * allocated via alloc_contig(AC_ALIGN4K).  Virtual and physical addresses are
 * managed by ohci_mem.c.
 *
 * Alignment summary:
 *   ED   — 16 bytes, 16-byte aligned (spec §4.2)
 *   TD   — 16 bytes, 16-byte aligned (spec §4.3, general TD only)
 *   HCCA — 256 bytes, 256-byte aligned (spec §4.4)
 */

#ifndef _OHCI_STRUCTS_H_
#define _OHCI_STRUCTS_H_

#include <stdint.h>

#include <usbd/hcd_common.h>	/* hcd_reg4 */
#include "ohci_regs.h"


/*===========================================================================*
 *    Endpoint Descriptor (ED)  —  spec §4.2                                *
 *===========================================================================*/
struct ohci_ed {
	hcd_reg4 control;	/* FA / EN / D / S / K / F / MPS (OHCI_ED_*)   */
	hcd_reg4 tailp;		/* TailP — physical pointer to last TD          */
	hcd_reg4 headp;		/* HeadP — phys ptr + H + dataToggleCarry bits  */
	hcd_reg4 nexted;	/* NextED — physical pointer to next ED in list */
};
/* §4.2: ED pools must be 16-byte aligned — satisfied by alloc_contig(AC_ALIGN4K)
 * and the natural 16-byte stride of struct ohci_ed. */


/*===========================================================================*
 *    Transfer Descriptor (TD, general)  —  spec §4.3.1                     *
 *                                                                           *
 *    Isochronous TDs (spec §4.3.2) are 32 bytes; deferred to a later phase. *
 *===========================================================================*/
struct ohci_td {
	hcd_reg4 control;	/* CC / EC / T / DI / DP / R (OHCI_TD_*)        */
	hcd_reg4 cbp;		/* CurrentBufferPointer — phys, hardware-updated*/
	hcd_reg4 nexttd;	/* NextTD — physical pointer (low 4 bits MBZ)   */
	hcd_reg4 be;		/* BufferEnd — phys of last byte of buffer      */
};
/* §4.3.1: TD pools must be 16-byte aligned — satisfied by alloc_contig and
 * the natural 16-byte stride of struct ohci_td. */


/*===========================================================================*
 *    HCCA (Host Controller Communications Area)  —  spec §4.4              *
 *                                                                           *
 *    Single 256-byte structure shared between software and HC.  Must be    *
 *    256-byte aligned in physical memory.                                  *
 *===========================================================================*/
struct ohci_hcca {
	hcd_reg4 int_table[OHCI_HCCA_INT_TABLE_LEN];
				/* 32 phys ED pointers for periodic schedule    */
	uint16_t frame_number;	/* HccaFrameNumber — written by HC every SF    */
	uint16_t _pad1;		/* HccaPad1 — must be zeroed by software       */
	hcd_reg4 done_head;	/* HccaDoneHead — head of finished-TD list      */
	uint8_t  _reserved[120];/* 116 B reserved for HC + 4 B alignment pad    */
};


/*===========================================================================*
 *    Compile-time size assertions                                           *
 *===========================================================================*/
typedef char _ohci_ed_size_check
	[(sizeof(struct ohci_ed)   == 16)  ? 1 : -1];
typedef char _ohci_td_size_check
	[(sizeof(struct ohci_td)   == 16)  ? 1 : -1];
typedef char _ohci_hcca_size_check
	[(sizeof(struct ohci_hcca) == 256) ? 1 : -1];


#endif /* !_OHCI_STRUCTS_H_ */
