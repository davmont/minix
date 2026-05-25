/*
 * OHCI DMA memory pool implementation.
 *
 * Four separate alloc_contig() calls keep each pool page-aligned and make
 * address arithmetic trivial:
 *
 *   hcca                                                (256 bytes)
 *   ed_virt[i]  = ed_base  + i * sizeof(struct ohci_ed) (16 bytes each)
 *   td_virt[i]  = td_base  + i * sizeof(struct ohci_td) (16 bytes each)
 *   xfer_buf                                            (MAX_WTOTALLENGTH)
 *
 * Physical addresses follow the same arithmetic from the corresponding
 * phys_bytes base returned by alloc_contig().
 *
 * The TD free-list is a 64-bit bitmask: bit i = 1 means td[i] is free.
 * EDs are referenced directly by index (sentinels + device); no free-list.
 */

#include <stdlib.h>
#include <string.h>

#include <minix/syslib.h>	/* alloc_contig, free_contig, phys_bytes */

#include <usbd/usbd_common.h>	/* USB_MSG */

#include "ohci_mem.h"


/*===========================================================================*
 *    Module-level storage                                                   *
 *===========================================================================*/
static struct ohci_hcca *hcca_base;
static phys_bytes        hcca_phys_base;

static struct ohci_ed   *ed_base;
static phys_bytes        ed_phys_base;

static struct ohci_td   *td_base;
static phys_bytes        td_phys_base;

static uint8_t          *xfer_buf_base;
static phys_bytes        xfer_buf_phys_base;

/* Bitmask: 1 = free slot, 0 = in use.  uint64_t covers OHCI_NUM_TD ≤ 64. */
static uint64_t          td_free_mask;


/*===========================================================================*
 *    ohci_mem_init                                                          *
 *===========================================================================*/
int
ohci_mem_init(void)
{
	size_t ed_size = OHCI_NUM_ED * sizeof(struct ohci_ed);
	size_t td_size = OHCI_NUM_TD * sizeof(struct ohci_td);

	/* HCCA — must be at least 256-byte aligned; 4 KiB alignment is fine. */
	hcca_base = alloc_contig(sizeof(struct ohci_hcca), AC_ALIGN4K,
				 &hcca_phys_base);
	if (hcca_base == NULL) {
		USB_MSG("OHCI mem: failed to allocate HCCA (%zu bytes)",
			sizeof(struct ohci_hcca));
		return EXIT_FAILURE;
	}
	memset(hcca_base, 0, sizeof(struct ohci_hcca));

	/* ED pool */
	ed_base = alloc_contig(ed_size, AC_ALIGN4K, &ed_phys_base);
	if (ed_base == NULL) {
		USB_MSG("OHCI mem: failed to allocate ED pool (%zu bytes)",
			ed_size);
		free_contig(hcca_base, sizeof(struct ohci_hcca));
		hcca_base = NULL;
		return EXIT_FAILURE;
	}
	memset(ed_base, 0, ed_size);

	/* TD pool */
	td_base = alloc_contig(td_size, AC_ALIGN4K, &td_phys_base);
	if (td_base == NULL) {
		USB_MSG("OHCI mem: failed to allocate TD pool (%zu bytes)",
			td_size);
		free_contig(ed_base,   ed_size);
		free_contig(hcca_base, sizeof(struct ohci_hcca));
		ed_base   = NULL;
		hcca_base = NULL;
		return EXIT_FAILURE;
	}
	memset(td_base, 0, td_size);

	/* Transfer data buffer */
	xfer_buf_base = alloc_contig(MAX_WTOTALLENGTH, AC_ALIGN4K,
				     &xfer_buf_phys_base);
	if (xfer_buf_base == NULL) {
		USB_MSG("OHCI mem: failed to allocate transfer buffer (%u bytes)",
			MAX_WTOTALLENGTH);
		free_contig(td_base,   td_size);
		free_contig(ed_base,   ed_size);
		free_contig(hcca_base, sizeof(struct ohci_hcca));
		td_base   = NULL;
		ed_base   = NULL;
		hcca_base = NULL;
		return EXIT_FAILURE;
	}
	memset(xfer_buf_base, 0, MAX_WTOTALLENGTH);

	/* All TD slots start free.  64-bit shift avoids the (1 << 64) UB even
	 * though OHCI_NUM_TD is currently 32 — keeps it safe if the pool grows. */
	td_free_mask = (OHCI_NUM_TD < 64)
			? ((1ULL << OHCI_NUM_TD) - 1ULL)
			: 0xFFFFFFFFFFFFFFFFULL;

	USB_MSG("OHCI mem: HCCA     virt=%p phys=0x%lx (%zu B)",
		(void *)hcca_base, (unsigned long)hcca_phys_base,
		sizeof(struct ohci_hcca));
	USB_MSG("OHCI mem: ED pool  virt=%p phys=0x%lx (%zu B)",
		(void *)ed_base,  (unsigned long)ed_phys_base,  ed_size);
	USB_MSG("OHCI mem: TD pool  virt=%p phys=0x%lx (%zu B)",
		(void *)td_base,  (unsigned long)td_phys_base,  td_size);
	USB_MSG("OHCI mem: xfer buf virt=%p phys=0x%lx (%u B)",
		(void *)xfer_buf_base, (unsigned long)xfer_buf_phys_base,
		MAX_WTOTALLENGTH);

	return EXIT_SUCCESS;
}


/*===========================================================================*
 *    ohci_mem_deinit                                                        *
 *===========================================================================*/
void
ohci_mem_deinit(void)
{
	if (xfer_buf_base != NULL) {
		free_contig(xfer_buf_base, MAX_WTOTALLENGTH);
		xfer_buf_base = NULL;
	}
	if (td_base != NULL) {
		free_contig(td_base, OHCI_NUM_TD * sizeof(struct ohci_td));
		td_base = NULL;
	}
	if (ed_base != NULL) {
		free_contig(ed_base, OHCI_NUM_ED * sizeof(struct ohci_ed));
		ed_base = NULL;
	}
	if (hcca_base != NULL) {
		free_contig(hcca_base, sizeof(struct ohci_hcca));
		hcca_base = NULL;
	}
	td_free_mask = 0;
}


/*===========================================================================*
 *    HCCA accessors                                                         *
 *===========================================================================*/
struct ohci_hcca *
ohci_hcca_virt(void)
{
	return hcca_base;
}

phys_bytes
ohci_hcca_phys(void)
{
	return hcca_phys_base;
}


/*===========================================================================*
 *    ED accessors                                                           *
 *===========================================================================*/
struct ohci_ed *
ohci_ed_virt(int idx)
{
	return &ed_base[idx];
}

phys_bytes
ohci_ed_phys(int idx)
{
	return ed_phys_base + (phys_bytes)(idx * (int)sizeof(struct ohci_ed));
}


/*===========================================================================*
 *    TD pool                                                                *
 *===========================================================================*/
int
ohci_td_alloc(void)
{
	int i;

	if (td_free_mask == 0) {
		USB_MSG("OHCI mem: TD pool exhausted");
		return -1;
	}

	/* Find lowest-numbered free slot */
	for (i = 0; i < OHCI_NUM_TD; i++) {
		if (td_free_mask & (1ULL << i)) {
			td_free_mask &= ~(1ULL << i);
			memset(&td_base[i], 0, sizeof(struct ohci_td));
			return i;
		}
	}

	return -1;	/* unreachable if td_free_mask != 0 */
}

void
ohci_td_free(int idx)
{
	td_free_mask |= (1ULL << idx);
}

struct ohci_td *
ohci_td_virt(int idx)
{
	return &td_base[idx];
}

phys_bytes
ohci_td_phys(int idx)
{
	return td_phys_base + (phys_bytes)(idx * (int)sizeof(struct ohci_td));
}


/*===========================================================================*
 *    Transfer data buffer                                                   *
 *===========================================================================*/
uint8_t *
ohci_xfer_buf_virt(void)
{
	return xfer_buf_base;
}

phys_bytes
ohci_xfer_buf_phys(void)
{
	return xfer_buf_phys_base;
}
