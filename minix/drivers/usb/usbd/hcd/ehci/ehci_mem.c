/*
 * EHCI DMA memory pool implementation.
 *
 * Three separate alloc_contig() calls keep each pool page-aligned and make
 * address arithmetic trivial:
 *
 *   qh_virt[i]   = qh_base  + i * sizeof(struct ehci_qh)   (64 bytes each)
 *   qtd_virt[i]  = qtd_base + i * sizeof(struct ehci_qtd)  (32 bytes each)
 *   xfer_buf                                                (MAX_WTOTALLENGTH)
 *
 * Physical addresses follow the same arithmetic from the corresponding
 * phys_bytes base returned by alloc_contig().
 *
 * The qTD free-list is a 32-bit bitmask: bit i = 1 means qTD[i] is free.
 */

#include <stdlib.h>
#include <string.h>

#include <minix/syslib.h>	/* alloc_contig, free_contig, phys_bytes */

#include <usbd/usbd_common.h>	/* USB_MSG */

#include "ehci_mem.h"


/*===========================================================================*
 *    Module-level storage                                                   *
 *===========================================================================*/
static struct ehci_qh  *qh_base;
static phys_bytes       qh_phys_base;

static struct ehci_qtd *qtd_base;
static phys_bytes       qtd_phys_base;

static uint8_t         *xfer_buf_base;
static phys_bytes       xfer_buf_phys_base;

/* Bitmask: 1 = free slot, 0 = in use.  Initialised to all-ones. */
static uint32_t         qtd_free_mask;


/*===========================================================================*
 *    ehci_mem_init                                                          *
 *===========================================================================*/
int
ehci_mem_init(void)
{
	size_t qh_size  = EHCI_NUM_QH  * sizeof(struct ehci_qh);
	size_t qtd_size = EHCI_NUM_QTD * sizeof(struct ehci_qtd);

	/* QH pool */
	qh_base = alloc_contig(qh_size, AC_ALIGN4K, &qh_phys_base);
	if (qh_base == NULL) {
		USB_MSG("EHCI mem: failed to allocate QH pool (%zu bytes)",
			qh_size);
		return EXIT_FAILURE;
	}
	memset(qh_base, 0, qh_size);

	/* qTD pool */
	qtd_base = alloc_contig(qtd_size, AC_ALIGN4K, &qtd_phys_base);
	if (qtd_base == NULL) {
		USB_MSG("EHCI mem: failed to allocate qTD pool (%zu bytes)",
			qtd_size);
		free_contig(qh_base, qh_size);
		qh_base = NULL;
		return EXIT_FAILURE;
	}
	memset(qtd_base, 0, qtd_size);

	/* Transfer data buffer */
	xfer_buf_base = alloc_contig(MAX_WTOTALLENGTH, AC_ALIGN4K,
				     &xfer_buf_phys_base);
	if (xfer_buf_base == NULL) {
		USB_MSG("EHCI mem: failed to allocate transfer buffer (%u bytes)",
			MAX_WTOTALLENGTH);
		free_contig(qtd_base, qtd_size);
		free_contig(qh_base,  qh_size);
		qtd_base = NULL;
		qh_base  = NULL;
		return EXIT_FAILURE;
	}
	memset(xfer_buf_base, 0, MAX_WTOTALLENGTH);

	/* All qTD slots start free.  Use 64-bit shift to avoid shift-count-
	 * overflow when EHCI_NUM_QTD == 32 (1u << 32 is UB on 32-bit types). */
	qtd_free_mask = (EHCI_NUM_QTD < 32)
			? (uint32_t)((1ULL << EHCI_NUM_QTD) - 1ULL)
			: 0xFFFFFFFFu;

	USB_MSG("EHCI mem: QH pool  virt=%p phys=0x%lx (%zu B)",
		(void *)qh_base,  (unsigned long)qh_phys_base,  qh_size);
	USB_MSG("EHCI mem: qTD pool virt=%p phys=0x%lx (%zu B)",
		(void *)qtd_base, (unsigned long)qtd_phys_base, qtd_size);
	USB_MSG("EHCI mem: xfer buf virt=%p phys=0x%lx (%u B)",
		(void *)xfer_buf_base, (unsigned long)xfer_buf_phys_base,
		MAX_WTOTALLENGTH);

	return EXIT_SUCCESS;
}


/*===========================================================================*
 *    ehci_mem_deinit                                                        *
 *===========================================================================*/
void
ehci_mem_deinit(void)
{
	if (xfer_buf_base != NULL) {
		free_contig(xfer_buf_base, MAX_WTOTALLENGTH);
		xfer_buf_base = NULL;
	}
	if (qtd_base != NULL) {
		free_contig(qtd_base, EHCI_NUM_QTD * sizeof(struct ehci_qtd));
		qtd_base = NULL;
	}
	if (qh_base != NULL) {
		free_contig(qh_base, EHCI_NUM_QH * sizeof(struct ehci_qh));
		qh_base = NULL;
	}
	qtd_free_mask = 0;
}


/*===========================================================================*
 *    QH accessors                                                           *
 *===========================================================================*/
struct ehci_qh *
ehci_qh_virt(int idx)
{
	return &qh_base[idx];
}

phys_bytes
ehci_qh_phys(int idx)
{
	return qh_phys_base + (phys_bytes)(idx * (int)sizeof(struct ehci_qh));
}


/*===========================================================================*
 *    qTD pool                                                               *
 *===========================================================================*/
int
ehci_qtd_alloc(void)
{
	int i;

	if (qtd_free_mask == 0) {
		USB_MSG("EHCI mem: qTD pool exhausted");
		return -1;
	}

	/* Find lowest-numbered free slot */
	for (i = 0; i < EHCI_NUM_QTD; i++) {
		if (qtd_free_mask & (1u << i)) {
			qtd_free_mask &= ~(1u << i);
			memset(&qtd_base[i], 0, sizeof(struct ehci_qtd));
			return i;
		}
	}

	return -1;	/* unreachable if qtd_free_mask != 0, but keep compiler happy */
}

void
ehci_qtd_free(int idx)
{
	qtd_free_mask |= (1u << idx);
}

struct ehci_qtd *
ehci_qtd_virt(int idx)
{
	return &qtd_base[idx];
}

phys_bytes
ehci_qtd_phys(int idx)
{
	return qtd_phys_base + (phys_bytes)(idx * (int)sizeof(struct ehci_qtd));
}


/*===========================================================================*
 *    Transfer data buffer                                                   *
 *===========================================================================*/
uint8_t *
ehci_xfer_buf_virt(void)
{
	return xfer_buf_base;
}

phys_bytes
ehci_xfer_buf_phys(void)
{
	return xfer_buf_phys_base;
}
