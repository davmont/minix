/*
 * EHCI DMA memory pool interface.
 *
 * Allocates and manages the physically contiguous, DMA-accessible memory
 * regions required by the EHCI host controller:
 *
 *   - Queue Head (QH) pool      — sentinel + one device QH
 *   - Transfer Descriptor pool  — EHCI_NUM_QTD reusable qTDs
 *   - Transfer data buffer      — one MAX_WTOTALLENGTH-byte DMA buffer
 *
 * All allocations are done via alloc_contig(AC_ALIGN4K) so that physical
 * addresses are known at init time and remain stable for the driver lifetime.
 */

#ifndef _EHCI_MEM_H_
#define _EHCI_MEM_H_

#include <minix/syslib.h>	/* phys_bytes */

#include <usbd/hcd_common.h>	/* MAX_WTOTALLENGTH */
#include "ehci_structs.h"


/*===========================================================================*
 *    Pool sizing constants                                                  *
 *===========================================================================*/
/*
 * QH pool: index 0 = async-ring sentinel; index 1 = single device QH.
 * (Phase 1 supports one device at a time, matching the MUSB limitation.)
 */
#define EHCI_QH_SENTINEL	0
#define EHCI_QH_DEVICE		1
#define EHCI_NUM_QH		2

/*
 * qTD pool: a control transfer needs at most 3 qTDs simultaneously
 * (SETUP + DATA + STATUS).  32 slots gives ample room.
 */
#define EHCI_NUM_QTD		32


/*===========================================================================*
 *    Lifecycle                                                              *
 *===========================================================================*/
int  ehci_mem_init(void);
void ehci_mem_deinit(void);


/*===========================================================================*
 *    QH accessors                                                          *
 *===========================================================================*/
/* Virtual pointer to QH at pool index idx */
struct ehci_qh *ehci_qh_virt(int idx);

/* Physical address of QH at pool index idx (for hardware registers) */
phys_bytes ehci_qh_phys(int idx);


/*===========================================================================*
 *    qTD pool                                                               *
 *===========================================================================*/
/* Allocate a free qTD; returns pool index, or -1 if pool is exhausted */
int ehci_qtd_alloc(void);

/* Release a previously allocated qTD back to the pool */
void ehci_qtd_free(int idx);

/* Virtual pointer to qTD at pool index idx */
struct ehci_qtd *ehci_qtd_virt(int idx);

/* Physical address of qTD at pool index idx */
phys_bytes ehci_qtd_phys(int idx);


/*===========================================================================*
 *    Transfer data buffer                                                   *
 *===========================================================================*/
/* Virtual pointer to the DMA transfer buffer (MAX_WTOTALLENGTH bytes) */
uint8_t *ehci_xfer_buf_virt(void);

/* Physical address of the DMA transfer buffer */
phys_bytes ehci_xfer_buf_phys(void);


#endif /* !_EHCI_MEM_H_ */
