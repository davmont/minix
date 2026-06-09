/*
 * xHCI DMA memory pool interface.
 *
 * Allocates the physically contiguous, DMA-accessible regions the xHCI
 * controller needs for bring-up:
 *
 *   DCBAA        — Device Context Base Address Array (64-bit entries)
 *   command ring — one segment of TRBs, terminated by a Link TRB
 *   event ring   — one segment of TRBs (primary interrupter)
 *   ERST         — Event Ring Segment Table (single entry)
 *   scratchpad   — optional scratchpad buffer array + page buffers
 *
 * Page-aligned allocations (alloc_contig, AC_ALIGN4K) trivially satisfy the
 * spec's 64-byte alignment requirements.
 */

#ifndef _XHCI_MEM_H_
#define _XHCI_MEM_H_

#include <stdint.h>

#include <minix/syslib.h>	/* phys_bytes */

#include "xhci_structs.h"


/*===========================================================================*
 *    Lifecycle                                                              *
 *===========================================================================*/
int  xhci_mem_init(void);
void xhci_mem_deinit(void);

/* Allocate the scratchpad buffer array and 'count' page buffers; returns the
 * physical address to store in DCBAA[0], or 0 if count == 0 / on failure. */
phys_bytes xhci_mem_scratchpad(int count);


/*===========================================================================*
 *    DCBAA (array of 64-bit context base pointers)                          *
 *===========================================================================*/
uint64_t  *xhci_dcbaa_virt(void);
phys_bytes xhci_dcbaa_phys(void);


/*===========================================================================*
 *    Command ring                                                           *
 *===========================================================================*/
struct xhci_trb *xhci_cmd_ring_virt(void);
phys_bytes       xhci_cmd_ring_phys(void);


/*===========================================================================*
 *    Event ring                                                            *
 *===========================================================================*/
struct xhci_trb *xhci_event_ring_virt(void);
phys_bytes       xhci_event_ring_phys(void);


/*===========================================================================*
 *    Event Ring Segment Table                                              *
 *===========================================================================*/
struct xhci_erst_entry *xhci_erst_virt(void);
phys_bytes              xhci_erst_phys(void);


#endif /* !_XHCI_MEM_H_ */
