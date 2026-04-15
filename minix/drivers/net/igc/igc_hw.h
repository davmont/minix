/* SPDX-License-Identifier: GPL-2.0 */
/* igc_hw.h - Hardware data structures for Intel I225/I226 2.5G Ethernet. */

#ifndef __IGC_HW_H
#define __IGC_HW_H

#include <stdint.h>

/*
 * Advanced Transmit Descriptor (16 bytes).
 *
 * The IGC requires advanced descriptors; legacy descriptors are not
 * supported.  A single-segment, non-offload send fills the fields as:
 *
 *   buffer_addr   = physical address of the packet data
 *   cmd_type_len  = length | DTYP | DEXT | IFCS | EOP | RS
 *   olinfo_status = paylen << IGC_ADVTXD_PAYLEN_SHIFT
 */
typedef struct igc_adv_tx_desc {
	uint64_t buffer_addr;
	uint32_t cmd_type_len;
	uint32_t olinfo_status;
} igc_adv_tx_desc_t;

/*
 * Advanced Receive Descriptor (16 bytes).
 *
 * The descriptor is written by the driver in "read" format and by the
 * hardware in "write-back" format.  Both formats occupy the same 16 bytes.
 */
typedef union igc_adv_rx_desc {
	/* Driver-written (read) format */
	struct {
		uint64_t pkt_addr;	/* Physical address of packet buffer */
		uint64_t hdr_addr;	/* Header buffer address (set to 0) */
	} read;

	/* Hardware write-back format */
	struct {
		struct {
			uint32_t mrq;
			uint32_t rss_hash;
		} lower;
		struct {
			uint32_t status_error;	/* Status/error bits */
			uint16_t length;	/* Packet length */
			uint16_t vlan;		/* VLAN tag */
		} upper;
	} wb;
} igc_adv_rx_desc_t;

/*
 * Advanced TX descriptor cmd_type_len field bits.
 */
#define IGC_ADVTXD_DTALEN_MASK		0x0000FFFF  /* Data buffer length */
#define IGC_ADVTXD_DTYP_DATA		0x00300000  /* Data descriptor type */
#define IGC_ADVTXD_DCMD_EOP		0x01000000  /* End of packet */
#define IGC_ADVTXD_DCMD_IFCS		0x02000000  /* Insert FCS */
#define IGC_ADVTXD_DCMD_RS		0x08000000  /* Report status */
#define IGC_ADVTXD_DCMD_DEXT		0x20000000  /* Advanced descriptor */

/*
 * Advanced TX descriptor olinfo_status field.
 * For non-TSO sends, set PAYLEN to the total packet length.
 */
#define IGC_ADVTXD_PAYLEN_SHIFT		14

/*
 * Advanced RX descriptor write-back status_error bits.
 */
#define IGC_RXD_STAT_DD			0x00000001  /* Descriptor done */
#define IGC_RXD_STAT_EOP		0x00000002  /* End of packet */

#endif /* __IGC_HW_H */
