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

/*
 * Maximum number of simultaneously-attached devices the driver backs with its
 * own (per-device) Device Context and EP0/bulk transfer rings.  Indexed by
 * root-hub port, so this must be >= HCD_MAX_PORTS (15).
 */
#define XHCI_MAX_DEVICES	16

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


/*===========================================================================*
 *    Device slot resources (single-device driver)                           *
 *                                                                           *
 *    Input Context and Device Context are byte-addressed because their      *
 *    per-entry stride (32 or 64 bytes) is decided at runtime from CSZ.      *
 *===========================================================================*/
/* Input Context is scratch, reused (serialised) across all devices */
uint8_t   *xhci_input_ctx_virt(void);
phys_bytes xhci_input_ctx_phys(void);

/* Per-device Device Context, indexed by root-hub port (0..XHCI_MAX_DEVICES-1).
 * Each slot's DCBAA entry points at its own context, so a second device's
 * Address Device does not overwrite the first's. */
uint8_t   *xhci_device_ctx_virt(int dev);
phys_bytes xhci_device_ctx_phys(int dev);

/* Per-device transfer rings: EP0 (control) and one bulk IN / bulk OUT ring */
struct xhci_trb *xhci_ep0_ring_virt(int dev);
phys_bytes       xhci_ep0_ring_phys(int dev);
struct xhci_trb *xhci_bulk_in_ring_virt(int dev);
phys_bytes       xhci_bulk_in_ring_phys(int dev);
struct xhci_trb *xhci_bulk_out_ring_virt(int dev);
phys_bytes       xhci_bulk_out_ring_phys(int dev);

/* DMA data buffer shared by control and bulk transfers */
uint8_t   *xhci_data_buf_virt(void);
phys_bytes xhci_data_buf_phys(void);


#endif /* !_XHCI_MEM_H_ */
