/* SPDX-License-Identifier: GPL-2.0 */
/* igc.h - Driver declarations for Intel I225/I226 2.5G Ethernet. */

#ifndef __IGC_H
#define __IGC_H

#include "igc_hw.h"

/*
 * Number of TX/RX descriptors per ring.  Must be a power of two and at least
 * 8 per the hardware requirement.
 */
#define IGC_RXDESC_NR	256
#define IGC_TXDESC_NR	256

/*
 * Per-descriptor I/O buffer size.  2048 bytes fits a full 1522-byte frame
 * (1500 data + Ethernet overhead + VLAN tag) with room to spare.
 */
#define IGC_IOBUF_SIZE	2048

/* Debug verbosity: 0 = none, higher = more output. */
#define IGC_VERBOSE	0

/* Environment variable for MAC address override. */
#define IGC_ENVVAR	"IGCETH"

#define IGC_DEBUG(level, args) \
	if ((level) <= IGC_VERBOSE) { printf args; }

/*
 * Driver instance state.
 */
typedef struct igc {
	int		 irq;		/* IRQ line */
	int		 irq_hook;	/* IRQ hook id returned by sys_irqsetpolicy */
	uint8_t		*regs;		/* MMIO base (mapped via vm_map_phys) */

	/* Receive ring */
	igc_adv_rx_desc_t *rx_desc;	/* Descriptor ring (physically contiguous) */
	int		 rx_desc_count;
	char		*rx_buffer;	/* Packet buffers (physically contiguous) */
	int		 rx_buffer_size;
	phys_bytes	 rx_buff_phys;	/* Physical base of rx_buffer */

	/* Transmit ring */
	igc_adv_tx_desc_t *tx_desc;	/* Descriptor ring (physically contiguous) */
	int		 tx_desc_count;
	char		*tx_buffer;	/* Packet buffers (physically contiguous) */
	int		 tx_buffer_size;
	phys_bytes	 tx_buff_phys;	/* Physical base of tx_buffer */
} igc_t;

#endif /* __IGC_H */
