/*
 * EHCI hardware descriptor structures.
 *
 * These map directly onto the memory layouts the EHCI hardware DMA-reads and
 * DMA-writes.  Sizes and alignments are dictated by the Intel EHCI
 * specification rev 1.0, §§3.5–3.6.
 *
 * All instances must reside in physically contiguous, DMA-accessible memory
 * allocated via alloc_contig(AC_ALIGN4K).  Virtual and physical addresses are
 * managed by ehci_mem.c.
 */

#ifndef _EHCI_STRUCTS_H_
#define _EHCI_STRUCTS_H_

#include <stdint.h>
#include <sys/cdefs.h>		/* __aligned() */

#include <usbd/hcd_common.h>	/* hcd_reg4 */
#include "ehci_regs.h"


/*===========================================================================*
 *    Transfer Descriptor (qTD)  —  EHCI spec §3.5                          *
 *                                                                           *
 *    32 bytes, must be 32-byte aligned.  Because 32 is also the struct      *
 *    size, arrays of qTDs are automatically element-aligned.                *
 *===========================================================================*/
struct ehci_qtd {
	hcd_reg4 next;		/* Next qTD pointer (EHCI_PTR_TERMINATE = last) */
	hcd_reg4 alt_next;	/* Alternate next (short-packet path)          */
	hcd_reg4 token;		/* Status, PID, toggle, byte count (EHCI_QTD_*)*/
	hcd_reg4 buf[5];	/* Up to 5 × 4 KB buffer page pointers         */
} __aligned(32);		/* §3.5: qTDs must be 32-byte aligned           */


/*===========================================================================*
 *    Queue Head (QH)  —  EHCI spec §3.6                                    *
 *                                                                           *
 *    Hardware layout: 4 header words + 1 qTD overlay = 48 bytes.          *
 *    Padded to 64 bytes so that an array keeps every element 32-byte       *
 *    aligned (64 is the smallest multiple of 32 that is ≥ 48).            *
 *===========================================================================*/
struct ehci_qh {
	hcd_reg4 horiz_link;	/* Horizontal link to next QH in ring          */
	hcd_reg4 endpoint;	/* Endpoint characteristics (addr/EP/speed/MPS)*/
	hcd_reg4 endpoint2;	/* Endpoint capabilities (hub/port/mask/mult)  */
	hcd_reg4 current_qtd;	/* Current qTD pointer — hardware-maintained   */
	struct ehci_qtd overlay;/* qTD overlay — hardware copies active qTD here*/
	hcd_reg4 _pad[4];	/* Pad to 64 bytes for array element alignment  */
} __aligned(32);		/* §3.6: QHs must be 32-byte aligned            */


/*===========================================================================*
 *    Compile-time size assertions                                           *
 *===========================================================================*/
typedef char _ehci_qtd_size_check
	[(sizeof(struct ehci_qtd) == 32) ? 1 : -1];
typedef char _ehci_qh_size_check
	[(sizeof(struct ehci_qh)  == 64) ? 1 : -1];


#endif /* !_EHCI_STRUCTS_H_ */
