/* SPDX-License-Identifier: GPL-2.0 */
/*
 * igc.c - Minix network driver for Intel I225/I226 2.5 Gigabit Ethernet.
 *
 * Copyright (C) 2026 David Montero
 * Based on the Linux igc driver, Copyright (C) 2018 Intel Corporation,
 * and the Minix e1000 driver, Copyright (C) 2009 Niek Linnenbank.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * The I225/I226 controllers require advanced descriptors for both RX and TX.
 * Only queue 0 is used; offload features (TSO, checksum) are not enabled so
 * that the descriptor format stays simple.
 *
 * MAC address initialisation: rather than implementing a full NVM read
 * sequence, we rely on the firmware having already written the burned-in
 * address to RAL[0]/RAH[0].  An environment variable override is still
 * supported for testing.
 */

#include <minix/drivers.h>
#include <minix/netdriver.h>
#include <machine/pci.h>
#include <sys/mman.h>
#include "assert.h"
#include "igc.h"
#include "igc_hw.h"
#include "igc_reg.h"
#include "igc_pci.h"

/* Forward declarations */
static int  igc_init(unsigned int instance, netdriver_addr_t *addr,
		uint32_t *caps, unsigned int *ticks);
static void igc_stop(void);
static void igc_set_mode(unsigned int mode, const netdriver_addr_t *mcast_list,
		unsigned int mcast_count);
static void igc_set_hwaddr(const netdriver_addr_t *addr);
static int  igc_send(struct netdriver_data *data, size_t size);
static ssize_t igc_recv(struct netdriver_data *data, size_t max);
static unsigned int igc_get_link(uint32_t *media);
static void igc_intr(unsigned int mask);
static void igc_tick(void);

static int  igc_probe(igc_t *e, int skip);
static int  igc_is_supported(uint16_t did);
static void igc_reset_hw(igc_t *e);
static void igc_init_hw(igc_t *e, netdriver_addr_t *addr);
static void igc_init_addr(igc_t *e, netdriver_addr_t *addr);
static void igc_init_buf(igc_t *e);

static uint32_t igc_reg_read(igc_t *e, uint32_t reg);
static void     igc_reg_write(igc_t *e, uint32_t reg, uint32_t value);
static void     igc_reg_set(igc_t *e, uint32_t reg, uint32_t value);
static void     igc_reg_unset(igc_t *e, uint32_t reg, uint32_t value);

static int igc_instance;
static igc_t igc_state;

static const struct netdriver igc_table = {
	.ndr_name	= "igc",
	.ndr_init	= igc_init,
	.ndr_stop	= igc_stop,
	.ndr_set_mode	= igc_set_mode,
	.ndr_set_hwaddr	= igc_set_hwaddr,
	.ndr_recv	= igc_recv,
	.ndr_send	= igc_send,
	.ndr_get_link	= igc_get_link,
	.ndr_intr	= igc_intr,
	.ndr_tick	= igc_tick,
};

int
main(int argc, char *argv[])
{
	env_setargs(argc, argv);
	netdriver_task(&igc_table);
	return 0;
}

/*
 * Initialise the driver and device for the given instance.
 */
static int
igc_init(unsigned int instance, netdriver_addr_t *addr, uint32_t *caps,
	unsigned int *ticks)
{
	igc_t *e;
	int r;

	igc_instance = instance;

	memset(&igc_state, 0, sizeof(igc_state));
	e = &igc_state;

	if ((r = tsc_calibrate()) != OK)
		panic("igc: tsc_calibrate failed: %d", r);

	if (!igc_probe(e, instance))
		return ENXIO;

	igc_init_hw(e, addr);

	*caps  = NDEV_CAP_MCAST | NDEV_CAP_BCAST | NDEV_CAP_HWADDR;
	*ticks = sys_hz() / 10;
	return OK;
}

/*
 * Scan the PCI bus for a supported device.  Skip the first <skip> matches so
 * that multiple cards can be driven by separate instances.  Returns TRUE on
 * success, FALSE if no matching card was found.
 */
static int
igc_probe(igc_t *e, int skip)
{
	int r, devind, ioflag;
	uint16_t vid, did, cr;
	uint32_t base, size;
	const char *dname;

	IGC_DEBUG(3, ("igc: probe(skip=%d)\n", skip));

	pci_init();

	if ((r = pci_first_dev(&devind, &vid, &did)) == 0)
		return FALSE;

	/* Advance past non-Intel or unsupported devices. */
	while (vid != IGC_VENDOR_ID || !igc_is_supported(did) || skip-- > 0) {
		if (!(r = pci_next_dev(&devind, &vid, &did)))
			return FALSE;
	}

	if (!(dname = pci_dev_name(vid, did)))
		dname = "Intel I225/I226 2.5G Ethernet";

	printf("igc: %s (%04x/%04x) at %s\n",
	    dname, vid, did, pci_slot_name(devind));

	pci_reserve(devind);

	e->irq = pci_attr_r8(devind, PCI_ILR);

	if ((r = pci_get_bar(devind, PCI_BAR, &base, &size, &ioflag)) != OK)
		panic("igc: failed to get PCI BAR: %d", r);
	if (ioflag)
		panic("igc: PCI BAR is not memory-mapped");

	if ((e->regs = vm_map_phys(SELF, (void *)(uintptr_t)base, size))
	    == MAP_FAILED)
		panic("igc: failed to map MMIO registers");

	/* Enable bus mastering for DMA. */
	cr = pci_attr_r16(devind, PCI_CR);
	if (!(cr & PCI_CR_MAST_EN))
		pci_attr_w16(devind, PCI_CR, cr | PCI_CR_MAST_EN);

	IGC_DEBUG(3, ("igc: MMIO at %p, IRQ %d\n", e->regs, e->irq));

	return TRUE;
}

/*
 * Return non-zero if <did> belongs to the IGC family.
 */
static int
igc_is_supported(uint16_t did)
{
	switch (did) {
	case IGC_DEV_ID_I225_LM:
	case IGC_DEV_ID_I225_V:
	case IGC_DEV_ID_I225_I:
	case IGC_DEV_ID_I225_IT:
	case IGC_DEV_ID_I225_BLANK_NVM:
	case IGC_DEV_ID_I226_LM:
	case IGC_DEV_ID_I226_V:
	case IGC_DEV_ID_I226_IT:
	case IGC_DEV_ID_I226_K:
	case IGC_DEV_ID_I226_LMVP:
		return 1;
	default:
		return 0;
	}
}

/*
 * Issue a full device reset and wait for it to complete.
 */
static void
igc_reset_hw(igc_t *e)
{
	/* Mask all interrupts before resetting. */
	igc_reg_write(e, IGC_REG_IMC, 0xFFFFFFFF);

	/* Trigger reset. */
	igc_reg_set(e, IGC_REG_CTRL, IGC_REG_CTRL_RST);

	/* The datasheet requires at least 3 ms after RST before any access. */
	micro_delay(20000);

	/* Re-mask interrupts; the reset clears IMC. */
	igc_reg_write(e, IGC_REG_IMC, 0xFFFFFFFF);
}

/*
 * Read the MAC address.
 *
 * We read the already-initialised RAL[0]/RAH[0] registers that the BIOS/UEFI
 * populates from NVM.  An environment variable of the form IGCETH#_EA can
 * override this for testing.
 */
static void
igc_init_addr(igc_t *e, netdriver_addr_t *addr)
{
	static char eakey[] = IGC_ENVVAR "#_EA";
	static char eafmt[] = "x:x:x:x:x:x";
	uint32_t ral;
	uint16_t rah;
	long v;
	int i;

	eakey[sizeof(IGC_ENVVAR) - 1] = '0' + igc_instance;

	for (i = 0; i < 6; i++) {
		if (env_parse(eakey, eafmt, i, &v, 0x00L, 0xFFL) != EP_SET)
			break;
		addr->na_addr[i] = (uint8_t)v;
	}

	if (i != 6) {
		/* Read from the receive address registers set by firmware. */
		ral = igc_reg_read(e, IGC_REG_RAL);
		rah = (uint16_t)igc_reg_read(e, IGC_REG_RAH);

		addr->na_addr[0] = (ral >>  0) & 0xFF;
		addr->na_addr[1] = (ral >>  8) & 0xFF;
		addr->na_addr[2] = (ral >> 16) & 0xFF;
		addr->na_addr[3] = (ral >> 24) & 0xFF;
		addr->na_addr[4] = (rah >>  0) & 0xFF;
		addr->na_addr[5] = (rah >>  8) & 0xFF;
	}

	/* Programme the address into the hardware filter. */
	igc_set_hwaddr(addr);

	IGC_DEBUG(1, ("igc: MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
	    addr->na_addr[0], addr->na_addr[1], addr->na_addr[2],
	    addr->na_addr[3], addr->na_addr[4], addr->na_addr[5]));
}

/*
 * Allocate and map DMA memory for the RX and TX descriptor rings and their
 * associated packet buffers, then programme the hardware ring registers.
 */
static void
igc_init_buf(igc_t *e)
{
	phys_bytes rx_desc_p, rx_buff_p;
	phys_bytes tx_desc_p, tx_buff_p;
	int i;

	e->rx_desc_count = IGC_RXDESC_NR;
	e->tx_desc_count = IGC_TXDESC_NR;

	/* --- Receive side --- */
	if ((e->rx_desc = alloc_contig(
	    sizeof(igc_adv_rx_desc_t) * e->rx_desc_count,
	    AC_ALIGN4K, &rx_desc_p)) == NULL)
		panic("igc: failed to allocate RX descriptor ring");

	memset(e->rx_desc, 0,
	    sizeof(igc_adv_rx_desc_t) * e->rx_desc_count);

	e->rx_buffer_size = IGC_RXDESC_NR * IGC_IOBUF_SIZE;

	if ((e->rx_buffer = alloc_contig(e->rx_buffer_size,
	    AC_ALIGN4K, &rx_buff_p)) == NULL)
		panic("igc: failed to allocate RX buffers");

	e->rx_buff_phys = rx_buff_p;

	/*
	 * Fill each descriptor with the physical address of its buffer.
	 * hdr_addr is set to 0 to use single-buffer mode.
	 */
	for (i = 0; i < IGC_RXDESC_NR; i++) {
		e->rx_desc[i].read.pkt_addr =
		    rx_buff_p + (phys_bytes)i * IGC_IOBUF_SIZE;
		e->rx_desc[i].read.hdr_addr = 0;
	}

	/* Programme the RX ring registers (queue 0). */
	igc_reg_write(e, IGC_REG_RDBAL, (uint32_t)(rx_desc_p & 0xFFFFFFFF));
	igc_reg_write(e, IGC_REG_RDBAH, (uint32_t)(rx_desc_p >> 32));
	igc_reg_write(e, IGC_REG_RDLEN,
	    (uint32_t)(e->rx_desc_count * sizeof(igc_adv_rx_desc_t)));
	igc_reg_write(e, IGC_REG_RDH, 0);
	igc_reg_write(e, IGC_REG_RDT, e->rx_desc_count - 1);

	/*
	 * Configure the receive buffer size via SRRCTL then enable the queue
	 * via RXDCTL.  RXDCTL.ENABLE must be set before RCTL.EN.
	 */
	igc_reg_write(e, IGC_REG_SRRCTL,
	    IGC_REG_SRRCTL_BSIZEPACKET_2K | IGC_REG_SRRCTL_DESCTYPE_ADV);
	igc_reg_set(e, IGC_REG_RXDCTL, IGC_REG_RXDCTL_ENABLE);

	/* Enable the receiver. */
	igc_reg_unset(e, IGC_REG_RCTL, IGC_REG_RCTL_BSIZE);
	igc_reg_set(e, IGC_REG_RCTL,
	    IGC_REG_RCTL_EN | IGC_REG_RCTL_BAM | IGC_REG_RCTL_SECRC);

	/* --- Transmit side --- */
	if ((e->tx_desc = alloc_contig(
	    sizeof(igc_adv_tx_desc_t) * e->tx_desc_count,
	    AC_ALIGN4K, &tx_desc_p)) == NULL)
		panic("igc: failed to allocate TX descriptor ring");

	memset(e->tx_desc, 0,
	    sizeof(igc_adv_tx_desc_t) * e->tx_desc_count);

	e->tx_buffer_size = IGC_TXDESC_NR * IGC_IOBUF_SIZE;

	if ((e->tx_buffer = alloc_contig(e->tx_buffer_size,
	    AC_ALIGN4K, &tx_buff_p)) == NULL)
		panic("igc: failed to allocate TX buffers");

	e->tx_buff_phys = tx_buff_p;

	/*
	 * Pre-fill the buffer addresses in the TX descriptors.  The command
	 * and length fields are set per-packet in igc_send().
	 */
	for (i = 0; i < IGC_TXDESC_NR; i++) {
		e->tx_desc[i].buffer_addr =
		    tx_buff_p + (phys_bytes)i * IGC_IOBUF_SIZE;
	}

	/* Programme the TX ring registers (queue 0). */
	igc_reg_write(e, IGC_REG_TDBAL, (uint32_t)(tx_desc_p & 0xFFFFFFFF));
	igc_reg_write(e, IGC_REG_TDBAH, (uint32_t)(tx_desc_p >> 32));
	igc_reg_write(e, IGC_REG_TDLEN,
	    (uint32_t)(e->tx_desc_count * sizeof(igc_adv_tx_desc_t)));
	igc_reg_write(e, IGC_REG_TDH, 0);
	igc_reg_write(e, IGC_REG_TDT, 0);

	/* Enable the TX queue. */
	igc_reg_set(e, IGC_REG_TXDCTL, IGC_REG_TXDCTL_ENABLE);

	/* Enable the transmitter. */
	igc_reg_set(e, IGC_REG_TCTL,
	    IGC_REG_TCTL_EN | IGC_REG_TCTL_PSP);
}

/*
 * Full hardware initialisation sequence.
 */
static void
igc_init_hw(igc_t *e, netdriver_addr_t *addr)
{
	int r, i;

	e->irq_hook = e->irq;

	if ((r = sys_irqsetpolicy(e->irq, 0, &e->irq_hook)) != OK)
		panic("igc: sys_irqsetpolicy failed: %d", r);
	if ((r = sys_irqenable(&e->irq_hook)) != OK)
		panic("igc: sys_irqenable failed: %d", r);

	igc_reset_hw(e);

	/*
	 * Basic controller setup per the I225/I226 Software Developer Manual,
	 * section "General Configuration":
	 *   - Set SLU to force link-up detection.
	 *   - Disable VLAN mode.
	 *   - Disable flow control.
	 */
	igc_reg_set(e, IGC_REG_CTRL, IGC_REG_CTRL_SLU);
	igc_reg_unset(e, IGC_REG_CTRL, IGC_REG_CTRL_VME);

	igc_reg_write(e, IGC_REG_FCAL,  0);
	igc_reg_write(e, IGC_REG_FCAH,  0);
	igc_reg_write(e, IGC_REG_FCT,   0);
	igc_reg_write(e, IGC_REG_FCTTV, 0);

	/* Set the standard inter-packet gap for 2.5G operation. */
	igc_reg_write(e, IGC_REG_TIPG, IGC_REG_TIPG_DEFAULT);

	/* Clear the Multicast Table Array. */
	for (i = 0; i < 128; i++)
		igc_reg_write(e, IGC_REG_MTA + i * 4, 0);

	/* Clear statistics counters. */
	for (i = 0; i < 64; i++)
		igc_reg_read(e, IGC_REG_CRCERRS + i * 4);

	igc_init_addr(e, addr);
	igc_init_buf(e);

	/* Enable interrupts we care about. */
	igc_reg_write(e, IGC_REG_IMS,
	    IGC_REG_ICR_LSC  |
	    IGC_REG_ICR_RXMISS |
	    IGC_REG_ICR_RXT  |
	    IGC_REG_ICR_TXQE |
	    IGC_REG_ICR_TXDW);
}

/*
 * Set the receive filter mode (promiscuous / multicast / broadcast).
 */
static void
igc_set_mode(unsigned int mode,
	const netdriver_addr_t *mcast_list __unused,
	unsigned int mcast_count __unused)
{
	igc_t *e;
	uint32_t rctl;

	e = &igc_state;

	rctl = igc_reg_read(e, IGC_REG_RCTL);
	rctl &= ~(IGC_REG_RCTL_BAM | IGC_REG_RCTL_MPE | IGC_REG_RCTL_UPE);

	if (mode & NDEV_MODE_BCAST)
		rctl |= IGC_REG_RCTL_BAM;
	if (mode & (NDEV_MODE_MCAST_LIST | NDEV_MODE_MCAST_ALL))
		rctl |= IGC_REG_RCTL_MPE;
	if (mode & NDEV_MODE_PROMISC)
		rctl |= IGC_REG_RCTL_BAM | IGC_REG_RCTL_MPE | IGC_REG_RCTL_UPE;

	igc_reg_write(e, IGC_REG_RCTL, rctl);
}

/*
 * Programme a new MAC address into the hardware receive filter.
 */
static void
igc_set_hwaddr(const netdriver_addr_t *hwaddr)
{
	igc_t *e;
	uint32_t ral;
	uint32_t rah;

	e = &igc_state;

	memcpy(&ral, &hwaddr->na_addr[0], sizeof(ral));
	rah  = (uint32_t)hwaddr->na_addr[4];
	rah |= (uint32_t)hwaddr->na_addr[5] << 8;

	igc_reg_write(e, IGC_REG_RAL, ral);
	igc_reg_write(e, IGC_REG_RAH, rah | IGC_REG_RAH_AV);
}

/*
 * Transmit a packet.  Called by the netdriver framework when there is a packet
 * to send.  Returns OK on success or SUSPEND if the TX ring is full.
 */
static int
igc_send(struct netdriver_data *data, size_t size)
{
	igc_t *e;
	igc_adv_tx_desc_t *desc;
	unsigned int head, tail, next;
	char *ptr;

	e = &igc_state;

	if (size > IGC_IOBUF_SIZE)
		panic("igc: packet too large (%zu > %d)", size, IGC_IOBUF_SIZE);

	head = igc_reg_read(e, IGC_REG_TDH);
	tail = igc_reg_read(e, IGC_REG_TDT);
	next = (tail + 1) % e->tx_desc_count;

	if (next == head)
		return SUSPEND;

	desc = &e->tx_desc[tail];
	ptr  = e->tx_buffer + tail * IGC_IOBUF_SIZE;

	netdriver_copyin(data, 0, ptr, size);

	/*
	 * Build an advanced data descriptor.  DEXT must be set to distinguish
	 * advanced from legacy descriptors; DTYP selects "data" type (0x3).
	 */
	desc->olinfo_status = (uint32_t)size << IGC_ADVTXD_PAYLEN_SHIFT;
	desc->cmd_type_len  =
	    ((uint32_t)size & IGC_ADVTXD_DTALEN_MASK) |
	    IGC_ADVTXD_DTYP_DATA  |
	    IGC_ADVTXD_DCMD_DEXT  |
	    IGC_ADVTXD_DCMD_IFCS  |
	    IGC_ADVTXD_DCMD_EOP   |
	    IGC_ADVTXD_DCMD_RS;

	igc_reg_write(e, IGC_REG_TDT, next);

	return OK;
}

/*
 * Receive a packet.  Called by the netdriver framework when an interrupt
 * indicates that packets may be available.  Returns the packet size on
 * success or SUSPEND if no complete packet is ready.
 */
static ssize_t
igc_recv(struct netdriver_data *data, size_t max)
{
	igc_t *e;
	igc_adv_rx_desc_t *desc;
	unsigned int tail, cur;
	char *ptr;
	size_t size;
	uint32_t status;

	e = &igc_state;

	tail = igc_reg_read(e, IGC_REG_RDT);
	cur  = (tail + 1) % e->rx_desc_count;
	desc = &e->rx_desc[cur];

	status = desc->wb.upper.status_error;

	if (!(status & IGC_RXD_STAT_DD))
		return SUSPEND;

	if (!(status & IGC_RXD_STAT_EOP))
		panic("igc: received multi-buffer packet (not supported)");

	size = desc->wb.upper.length;
	ptr  = e->rx_buffer + cur * IGC_IOBUF_SIZE;

	if (size > max)
		size = max;

	netdriver_copyout(data, 0, ptr, size);

	/*
	 * Hand the descriptor back to the hardware by clearing it and
	 * re-filling the buffer address, then advancing the tail.
	 */
	memset(desc, 0, sizeof(*desc));
	desc->read.pkt_addr = e->rx_buff_phys + (phys_bytes)cur * IGC_IOBUF_SIZE;
	desc->read.hdr_addr = 0;

	igc_reg_write(e, IGC_REG_RDT, cur);

	return (ssize_t)size;
}

/*
 * Return the current link state and media type.
 */
static unsigned int
igc_get_link(uint32_t *media)
{
	uint32_t status, type;

	status = igc_reg_read(&igc_state, IGC_REG_STATUS);

	if (!(status & IGC_REG_STATUS_LU))
		return NDEV_LINK_DOWN;

	type = (status & IGC_REG_STATUS_FD) ? IFM_ETHER | IFM_FDX
	                                     : IFM_ETHER | IFM_HDX;

	switch (status & IGC_REG_STATUS_SPEED) {
	case IGC_REG_STATUS_SPEED_10:
		type |= IFM_10_T;
		break;
	case IGC_REG_STATUS_SPEED_100:
		type |= IFM_100_TX;
		break;
	case IGC_REG_STATUS_SPEED_1000:
		type |= IFM_1000_T;
		break;
	case IGC_REG_STATUS_SPEED_2500:
		type |= IFM_2500_T;
		break;
	}

	*media = type;
	return NDEV_LINK_UP;
}

/*
 * Handle a hardware interrupt.
 */
static void
igc_intr(unsigned int __unused mask)
{
	igc_t *e;
	uint32_t cause;

	e = &igc_state;

	/* Re-arm the IRQ line. */
	if (sys_irqenable(&e->irq_hook) != OK)
		panic("igc: failed to re-enable IRQ");

	/* Reading ICR clears all pending interrupt bits. */
	cause = igc_reg_read(e, IGC_REG_ICR);

	if (cause & IGC_REG_ICR_LSC)
		netdriver_link();

	if (cause & (IGC_REG_ICR_RXMISS | IGC_REG_ICR_RXT))
		netdriver_recv();

	if (cause & (IGC_REG_ICR_TXQE | IGC_REG_ICR_TXDW))
		netdriver_send();
}

/*
 * Periodic tick: accumulate hardware error counters into the netdriver
 * statistics.  Counters are clear-on-read so we must poll them regularly.
 */
static void
igc_tick(void)
{
	igc_t *e;

	e = &igc_state;

	netdriver_stat_ierror(igc_reg_read(e, IGC_REG_RXERRC));
	netdriver_stat_ierror(igc_reg_read(e, IGC_REG_CRCERRS));
	netdriver_stat_ierror(igc_reg_read(e, IGC_REG_MPC));
	netdriver_stat_coll(igc_reg_read(e,   IGC_REG_COLC));
}

/*
 * Reset and disable the hardware cleanly on driver shutdown.
 */
static void
igc_stop(void)
{
	igc_reset_hw(&igc_state);
}

/* --------------------------------------------------------------------------
 * Register access helpers
 * -------------------------------------------------------------------------- */

static uint32_t
igc_reg_read(igc_t *e, uint32_t reg)
{
	assert(reg < 0x20000);
	return *(volatile uint32_t *)(e->regs + reg);
}

static void
igc_reg_write(igc_t *e, uint32_t reg, uint32_t value)
{
	assert(reg < 0x20000);
	*(volatile uint32_t *)(e->regs + reg) = value;
}

static void
igc_reg_set(igc_t *e, uint32_t reg, uint32_t value)
{
	igc_reg_write(e, reg, igc_reg_read(e, reg) | value);
}

static void
igc_reg_unset(igc_t *e, uint32_t reg, uint32_t value)
{
	igc_reg_write(e, reg, igc_reg_read(e, reg) & ~value);
}
