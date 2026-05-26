/*
 * OHCI DMA memory pool interface.
 *
 * Allocates and manages the physically contiguous, DMA-accessible regions
 * required by the OHCI host controller:
 *
 *   - HCCA          — single 256-byte communications area shared with HC
 *   - ED pool       — OHCI_NUM_ED reusable Endpoint Descriptors
 *   - TD pool       — OHCI_NUM_TD reusable Transfer Descriptors (general)
 *   - Transfer data — one MAX_WTOTALLENGTH-byte DMA buffer
 *
 * Allocations are done via alloc_contig(AC_ALIGN4K) so that the natural
 * 4 KiB alignment trivially satisfies the spec's 16/256-byte requirements.
 *
 * Free lists for the ED/TD pools are 64-bit bitmasks; bit i = 1 means slot
 * i is free.  Pool sizes are kept ≤ 64 so a single uint64_t covers them.
 */

#ifndef _OHCI_MEM_H_
#define _OHCI_MEM_H_

#include <stdint.h>

#include <minix/syslib.h>	/* phys_bytes */

#include <usbd/hcd_common.h>	/* MAX_WTOTALLENGTH */
#include "ohci_structs.h"


/*===========================================================================*
 *    Pool sizing constants                                                  *
 *===========================================================================*/
/*
 * ED pool: index 0 = control-list head (sentinel), 1 = bulk-list head
 * (sentinel), 2 = single device-endpoint ED for Phase 3 transfers.
 * Phase 3 (like EHCI Phase 1) supports one device at a time; later phases
 * will widen this.
 */
#define OHCI_ED_CTRL_HEAD	0
#define OHCI_ED_BULK_HEAD	1
#define OHCI_ED_DEVICE		2
#define OHCI_NUM_ED		8	/* sentinels + device ED + headroom    */

/*
 * TD pool: a control transfer needs at most 3 TDs (SETUP + DATA + STATUS),
 * plus the spec requires one always-present dummy TD per ED tail.  32 slots
 * matches EHCI's qTD pool and gives ample room.
 */
#define OHCI_NUM_TD		32


/*===========================================================================*
 *    Lifecycle                                                              *
 *===========================================================================*/
int  ohci_mem_init(void);
void ohci_mem_deinit(void);


/*===========================================================================*
 *    HCCA accessors                                                         *
 *===========================================================================*/
struct ohci_hcca *ohci_hcca_virt(void);
phys_bytes        ohci_hcca_phys(void);


/*===========================================================================*
 *    ED accessors                                                           *
 *===========================================================================*/
/* Virtual pointer to ED at pool index idx */
struct ohci_ed *ohci_ed_virt(int idx);

/* Physical address of ED at pool index idx (for hardware registers) */
phys_bytes ohci_ed_phys(int idx);


/*===========================================================================*
 *    TD pool                                                                *
 *===========================================================================*/
/* Allocate a free TD; returns pool index, or -1 if pool is exhausted */
int ohci_td_alloc(void);

/* Release a previously allocated TD back to the pool */
void ohci_td_free(int idx);

/* Virtual pointer to TD at pool index idx */
struct ohci_td *ohci_td_virt(int idx);

/* Physical address of TD at pool index idx */
phys_bytes ohci_td_phys(int idx);


/*===========================================================================*
 *    Transfer data buffer                                                   *
 *===========================================================================*/
/* Virtual pointer to the DMA transfer buffer (MAX_WTOTALLENGTH bytes) */
uint8_t *ohci_xfer_buf_virt(void);

/* Physical address of the DMA transfer buffer */
phys_bytes ohci_xfer_buf_phys(void);


#endif /* !_OHCI_MEM_H_ */
