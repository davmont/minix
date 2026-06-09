/*
 * xHCI DMA memory pool implementation.
 *
 * Each region gets its own page-aligned alloc_contig() allocation, which
 * keeps physical-address arithmetic trivial and satisfies every xHCI
 * alignment requirement (the strictest is 64 bytes).
 */

#include <stdlib.h>
#include <string.h>

#include <minix/syslib.h>	/* alloc_contig, free_contig, phys_bytes */

#include <usbd/usbd_common.h>	/* USB_MSG */

#include "xhci_mem.h"
#include "xhci_regs.h"


#define XHCI_PAGE		0x1000u
#define XHCI_MAX_SCRATCHPAD	64	/* upper bound we are willing to back */


/*===========================================================================*
 *    Module-level storage                                                   *
 *===========================================================================*/
static uint64_t        *dcbaa_base;
static phys_bytes       dcbaa_phys_base;

static struct xhci_trb *cmd_ring_base;
static phys_bytes       cmd_ring_phys_base;

static struct xhci_trb *event_ring_base;
static phys_bytes       event_ring_phys_base;

static struct xhci_erst_entry *erst_base;
static phys_bytes              erst_phys_base;

static uint64_t        *scratchpad_arr;	/* array of buffer phys pointers */
static phys_bytes       scratchpad_arr_phys;
static void            *scratchpad_bufs;	/* the backing pages */
static int              scratchpad_count;

/* Input Context (scratch, shared) and shared DMA data buffer */
static uint8_t         *input_ctx_base;
static phys_bytes       input_ctx_phys_base;
static uint8_t         *data_buf_base;
static phys_bytes       data_buf_phys_base;

/* Per-device slot resources, indexed by root-hub port */
static uint8_t         *device_ctx_base[XHCI_MAX_DEVICES];
static phys_bytes       device_ctx_phys_base[XHCI_MAX_DEVICES];
static struct xhci_trb *ep0_ring_base[XHCI_MAX_DEVICES];
static phys_bytes       ep0_ring_phys_base[XHCI_MAX_DEVICES];
static struct xhci_trb *bulk_in_ring_base[XHCI_MAX_DEVICES];
static phys_bytes       bulk_in_ring_phys_base[XHCI_MAX_DEVICES];
static struct xhci_trb *bulk_out_ring_base[XHCI_MAX_DEVICES];
static phys_bytes       bulk_out_ring_phys_base[XHCI_MAX_DEVICES];


/*===========================================================================*
 *    xhci_mem_init                                                          *
 *===========================================================================*/
int
xhci_mem_init(void)
{
	dcbaa_base = alloc_contig(XHCI_PAGE, AC_ALIGN4K, &dcbaa_phys_base);
	if (dcbaa_base == NULL)
		goto fail;
	memset(dcbaa_base, 0, XHCI_PAGE);

	cmd_ring_base = alloc_contig(XHCI_PAGE, AC_ALIGN4K, &cmd_ring_phys_base);
	if (cmd_ring_base == NULL)
		goto fail;
	memset(cmd_ring_base, 0, XHCI_PAGE);

	event_ring_base = alloc_contig(XHCI_PAGE, AC_ALIGN4K,
				       &event_ring_phys_base);
	if (event_ring_base == NULL)
		goto fail;
	memset(event_ring_base, 0, XHCI_PAGE);

	erst_base = alloc_contig(XHCI_PAGE, AC_ALIGN4K, &erst_phys_base);
	if (erst_base == NULL)
		goto fail;
	memset(erst_base, 0, XHCI_PAGE);

	/* Shared scratch input context + shared DMA data buffer */
	input_ctx_base = alloc_contig(XHCI_PAGE, AC_ALIGN4K,
				      &input_ctx_phys_base);
	data_buf_base = alloc_contig(MAX_WTOTALLENGTH, AC_ALIGN4K,
				     &data_buf_phys_base);
	if (input_ctx_base == NULL || data_buf_base == NULL)
		goto fail;
	memset(input_ctx_base, 0, XHCI_PAGE);
	memset(data_buf_base,  0, MAX_WTOTALLENGTH);

	/* Per-device slot resources (Device Context + EP0/bulk rings) */
	{
		int d;
		for (d = 0; d < XHCI_MAX_DEVICES; d++) {
			device_ctx_base[d] = alloc_contig(XHCI_PAGE, AC_ALIGN4K,
						&device_ctx_phys_base[d]);
			ep0_ring_base[d] = alloc_contig(XHCI_PAGE, AC_ALIGN4K,
						&ep0_ring_phys_base[d]);
			bulk_in_ring_base[d] = alloc_contig(XHCI_PAGE, AC_ALIGN4K,
						&bulk_in_ring_phys_base[d]);
			bulk_out_ring_base[d] = alloc_contig(XHCI_PAGE, AC_ALIGN4K,
						&bulk_out_ring_phys_base[d]);
			if (device_ctx_base[d] == NULL ||
			    ep0_ring_base[d] == NULL ||
			    bulk_in_ring_base[d] == NULL ||
			    bulk_out_ring_base[d] == NULL)
				goto fail;
			memset(device_ctx_base[d],   0, XHCI_PAGE);
			memset(ep0_ring_base[d],     0, XHCI_PAGE);
			memset(bulk_in_ring_base[d], 0, XHCI_PAGE);
			memset(bulk_out_ring_base[d],0, XHCI_PAGE);
		}
	}

	USB_MSG("xHCI mem: DCBAA virt=%p phys=0x%lx",
		(void *)dcbaa_base, (unsigned long)dcbaa_phys_base);
	USB_MSG("xHCI mem: cmd-ring virt=%p phys=0x%lx",
		(void *)cmd_ring_base, (unsigned long)cmd_ring_phys_base);
	USB_MSG("xHCI mem: evt-ring virt=%p phys=0x%lx",
		(void *)event_ring_base, (unsigned long)event_ring_phys_base);
	USB_MSG("xHCI mem: ERST virt=%p phys=0x%lx",
		(void *)erst_base, (unsigned long)erst_phys_base);

	return EXIT_SUCCESS;

fail:
	USB_MSG("xHCI mem: allocation failed");
	xhci_mem_deinit();
	return EXIT_FAILURE;
}


/*===========================================================================*
 *    xhci_mem_scratchpad                                                    *
 *===========================================================================*/
phys_bytes
xhci_mem_scratchpad(int count)
{
	int i;

	if (count <= 0)
		return 0;
	if (count > XHCI_MAX_SCRATCHPAD)
		count = XHCI_MAX_SCRATCHPAD;

	/* Array of 64-bit pointers, one per scratchpad buffer */
	scratchpad_arr = alloc_contig(XHCI_PAGE, AC_ALIGN4K,
				      &scratchpad_arr_phys);
	if (scratchpad_arr == NULL)
		return 0;
	memset(scratchpad_arr, 0, XHCI_PAGE);

	/* One physically contiguous block of 'count' page buffers */
	{
		phys_bytes bufs_phys;
		scratchpad_bufs = alloc_contig((size_t)count * XHCI_PAGE,
					       AC_ALIGN4K, &bufs_phys);
		if (scratchpad_bufs == NULL) {
			free_contig(scratchpad_arr, XHCI_PAGE);
			scratchpad_arr = NULL;
			return 0;
		}
		memset(scratchpad_bufs, 0, (size_t)count * XHCI_PAGE);

		for (i = 0; i < count; i++)
			scratchpad_arr[i] =
				(uint64_t)(bufs_phys + (phys_bytes)i * XHCI_PAGE);
	}

	scratchpad_count = count;
	USB_MSG("xHCI mem: %d scratchpad buffer(s), array phys=0x%lx",
		count, (unsigned long)scratchpad_arr_phys);

	return scratchpad_arr_phys;
}


/*===========================================================================*
 *    xhci_mem_deinit                                                        *
 *===========================================================================*/
void
xhci_mem_deinit(void)
{
	if (scratchpad_bufs != NULL) {
		free_contig(scratchpad_bufs,
			    (size_t)scratchpad_count * XHCI_PAGE);
		scratchpad_bufs = NULL;
	}
	if (scratchpad_arr != NULL) {
		free_contig(scratchpad_arr, XHCI_PAGE);
		scratchpad_arr = NULL;
	}
	if (erst_base != NULL) {
		free_contig(erst_base, XHCI_PAGE);
		erst_base = NULL;
	}
	if (event_ring_base != NULL) {
		free_contig(event_ring_base, XHCI_PAGE);
		event_ring_base = NULL;
	}
	if (cmd_ring_base != NULL) {
		free_contig(cmd_ring_base, XHCI_PAGE);
		cmd_ring_base = NULL;
	}
	if (dcbaa_base != NULL) {
		free_contig(dcbaa_base, XHCI_PAGE);
		dcbaa_base = NULL;
	}
	if (data_buf_base != NULL) {
		free_contig(data_buf_base, MAX_WTOTALLENGTH);
		data_buf_base = NULL;
	}
	{
		int d;
		for (d = 0; d < XHCI_MAX_DEVICES; d++) {
			if (bulk_out_ring_base[d] != NULL) {
				free_contig(bulk_out_ring_base[d], XHCI_PAGE);
				bulk_out_ring_base[d] = NULL;
			}
			if (bulk_in_ring_base[d] != NULL) {
				free_contig(bulk_in_ring_base[d], XHCI_PAGE);
				bulk_in_ring_base[d] = NULL;
			}
			if (ep0_ring_base[d] != NULL) {
				free_contig(ep0_ring_base[d], XHCI_PAGE);
				ep0_ring_base[d] = NULL;
			}
			if (device_ctx_base[d] != NULL) {
				free_contig(device_ctx_base[d], XHCI_PAGE);
				device_ctx_base[d] = NULL;
			}
		}
	}
	if (input_ctx_base != NULL) {
		free_contig(input_ctx_base, XHCI_PAGE);
		input_ctx_base = NULL;
	}
	scratchpad_count = 0;
}


/*===========================================================================*
 *    Accessors                                                              *
 *===========================================================================*/
uint64_t *xhci_dcbaa_virt(void)                { return dcbaa_base; }
phys_bytes xhci_dcbaa_phys(void)               { return dcbaa_phys_base; }

struct xhci_trb *xhci_cmd_ring_virt(void)      { return cmd_ring_base; }
phys_bytes xhci_cmd_ring_phys(void)            { return cmd_ring_phys_base; }

struct xhci_trb *xhci_event_ring_virt(void)    { return event_ring_base; }
phys_bytes xhci_event_ring_phys(void)          { return event_ring_phys_base; }

struct xhci_erst_entry *xhci_erst_virt(void)   { return erst_base; }
phys_bytes xhci_erst_phys(void)                { return erst_phys_base; }

uint8_t   *xhci_input_ctx_virt(void)   { return input_ctx_base; }
phys_bytes xhci_input_ctx_phys(void)   { return input_ctx_phys_base; }

uint8_t   *xhci_device_ctx_virt(int dev)  { return device_ctx_base[dev]; }
phys_bytes xhci_device_ctx_phys(int dev)  { return device_ctx_phys_base[dev]; }

struct xhci_trb *xhci_ep0_ring_virt(int dev)      { return ep0_ring_base[dev]; }
phys_bytes xhci_ep0_ring_phys(int dev)            { return ep0_ring_phys_base[dev]; }
struct xhci_trb *xhci_bulk_in_ring_virt(int dev)  { return bulk_in_ring_base[dev]; }
phys_bytes xhci_bulk_in_ring_phys(int dev)        { return bulk_in_ring_phys_base[dev]; }
struct xhci_trb *xhci_bulk_out_ring_virt(int dev) { return bulk_out_ring_base[dev]; }
phys_bytes xhci_bulk_out_ring_phys(int dev)       { return bulk_out_ring_phys_base[dev]; }

uint8_t   *xhci_data_buf_virt(void)    { return data_buf_base; }
phys_bytes xhci_data_buf_phys(void)    { return data_buf_phys_base; }
