/* SPDX-License-Identifier: GPL-2.0 */
/* igc_reg.h - MMIO register offsets and bit definitions for Intel I225/I226. */

#ifndef __IGC_REG_H
#define __IGC_REG_H

/*
 * General registers.
 */
#define IGC_REG_CTRL		0x00000  /* Device Control */
#define IGC_REG_STATUS		0x00008  /* Device Status */
#define IGC_REG_EECD		0x00010  /* EEPROM/Flash Control */
#define IGC_REG_EERD		0x00014  /* EEPROM Read */
#define IGC_REG_CTRL_EXT	0x00018  /* Extended Device Control */
#define IGC_REG_FCAL		0x00028  /* Flow Control Address Low */
#define IGC_REG_FCAH		0x0002C  /* Flow Control Address High */
#define IGC_REG_FCT		0x00030  /* Flow Control Type */
#define IGC_REG_FCTTV		0x00170  /* Flow Control Tx Timer Value */

/*
 * Interrupt registers.
 * Note: IGC moved the interrupt registers to 0x1500, unlike earlier Intel
 * GbE controllers that used 0x00C0.
 */
#define IGC_REG_ICR		0x01500  /* Interrupt Cause Read (clears on read) */
#define IGC_REG_ICS		0x01504  /* Interrupt Cause Set */
#define IGC_REG_IMS		0x01508  /* Interrupt Mask Set/Read */
#define IGC_REG_IMC		0x0150C  /* Interrupt Mask Clear */

/*
 * Receive registers.
 */
#define IGC_REG_RCTL		0x00100  /* Receive Control */
#define IGC_REG_RDBAL		0x02800  /* Rx Descriptor Base Address Low (Q0) */
#define IGC_REG_RDBAH		0x02804  /* Rx Descriptor Base Address High (Q0) */
#define IGC_REG_RDLEN		0x02808  /* Rx Descriptor Ring Length (Q0) */
#define IGC_REG_RDH		0x02810  /* Rx Descriptor Head (Q0) */
#define IGC_REG_RDT		0x02818  /* Rx Descriptor Tail (Q0) */
#define IGC_REG_RXDCTL		0x02828  /* Rx Descriptor Control (Q0) */
#define IGC_REG_SRRCTL		0x0C00C  /* Split and Replication Rx Control (Q0) */

/*
 * Transmit registers.
 */
#define IGC_REG_TCTL		0x00400  /* Transmit Control */
#define IGC_REG_TIPG		0x00410  /* Tx Inter-packet Gap */
#define IGC_REG_TDBAL		0x03800  /* Tx Descriptor Base Address Low (Q0) */
#define IGC_REG_TDBAH		0x03804  /* Tx Descriptor Base Address High (Q0) */
#define IGC_REG_TDLEN		0x03808  /* Tx Descriptor Ring Length (Q0) */
#define IGC_REG_TDH		0x03810  /* Tx Descriptor Head (Q0) */
#define IGC_REG_TDT		0x03818  /* Tx Descriptor Tail (Q0) */
#define IGC_REG_TXDCTL		0x03828  /* Tx Descriptor Control (Q0) */

/*
 * Statistics registers.  These are 32-bit, clear-on-read counters.
 */
#define IGC_REG_CRCERRS		0x04000  /* CRC Error Count */
#define IGC_REG_RXERRC		0x0400C  /* Rx Error Count */
#define IGC_REG_MPC		0x04010  /* Missed Packets Count */
#define IGC_REG_COLC		0x04028  /* Collision Count */
#define IGC_REG_TPR		0x040D0  /* Total Packets Received */
#define IGC_REG_TPT		0x040D4  /* Total Packets Transmitted */

/*
 * Address filtering.
 */
#define IGC_REG_RAL		0x05400  /* Receive Address Low [0] */
#define IGC_REG_RAH		0x05404  /* Receive Address High [0] */
#define IGC_REG_MTA		0x05200  /* Multicast Table Array (128 x 32-bit) */

/*
 * CTRL register bits.
 */
#define IGC_REG_CTRL_FD		(1u << 0)   /* Full Duplex */
#define IGC_REG_CTRL_SLU	(1u << 6)   /* Set Link Up */
#define IGC_REG_CTRL_RST	(1u << 26)  /* Device Reset */
#define IGC_REG_CTRL_VME	(1u << 30)  /* VLAN Mode Enable */

/*
 * STATUS register bits.
 */
#define IGC_REG_STATUS_FD	(1u << 0)   /* Link Full Duplex */
#define IGC_REG_STATUS_LU	(1u << 1)   /* Link Up */
#define IGC_REG_STATUS_SPEED	((1u << 6) | (1u << 7))  /* Speed mask */
#define IGC_REG_STATUS_SPEED_10		(0u << 6)
#define IGC_REG_STATUS_SPEED_100	(1u << 6)
#define IGC_REG_STATUS_SPEED_1000	(2u << 6)
#define IGC_REG_STATUS_SPEED_2500	(3u << 6)

/*
 * ICR / IMS / IMC register bits.
 */
#define IGC_REG_ICR_TXDW	(1u << 0)   /* Tx descriptor written back */
#define IGC_REG_ICR_TXQE	(1u << 1)   /* Tx queue empty */
#define IGC_REG_ICR_LSC		(1u << 2)   /* Link status change */
#define IGC_REG_ICR_RXMISS	(1u << 6)   /* Receiver missed packet */
#define IGC_REG_ICR_RXT		(1u << 7)   /* Rx timer interrupt */

/*
 * RCTL register bits.
 */
#define IGC_REG_RCTL_EN		(1u << 1)   /* Receive Enable */
#define IGC_REG_RCTL_UPE	(1u << 3)   /* Unicast Promiscuous */
#define IGC_REG_RCTL_MPE	(1u << 4)   /* Multicast Promiscuous */
#define IGC_REG_RCTL_BAM	(1u << 15)  /* Broadcast Accept */
#define IGC_REG_RCTL_BSIZE	((1u << 16) | (1u << 17))  /* Buffer size bits */
#define IGC_REG_RCTL_SECRC	(1u << 26)  /* Strip CRC */

/*
 * TCTL register bits.
 */
#define IGC_REG_TCTL_EN		(1u << 1)   /* Transmit Enable */
#define IGC_REG_TCTL_PSP	(1u << 3)   /* Pad Short Packets */
#define IGC_REG_TCTL_CT		(0x0Fu << 4) /* Collision Threshold */
#define IGC_REG_TCTL_COLD	(0x3Fu << 12) /* Collision Distance */

/*
 * TXDCTL / RXDCTL register bits.
 */
#define IGC_REG_TXDCTL_ENABLE	(1u << 25)  /* Queue Enable */
#define IGC_REG_RXDCTL_ENABLE	(1u << 25)  /* Queue Enable */

/*
 * RAH register bits.
 */
#define IGC_REG_RAH_AV		(1u << 31)  /* Receive Address Valid */

/*
 * SRRCTL register bits.
 * Used to configure the receive buffer size for advanced descriptors.
 * The BSIZEPACKET field (bits 6:0) is in units of 1 KB; the default of
 * 2 (2 KB) matches our IGC_IOBUF_SIZE.
 */
#define IGC_REG_SRRCTL_BSIZEPACKET_2K	0x02
#define IGC_REG_SRRCTL_DESCTYPE_ADV	0x00000000  /* Advanced one-buffer */

/*
 * EERD register bits (NVM read).
 */
#define IGC_REG_EERD_START	(1u << 0)   /* Start read */
#define IGC_REG_EERD_DONE	(1u << 1)   /* Read done */
#define IGC_REG_EERD_ADDR_SHIFT	2
#define IGC_REG_EERD_DATA_SHIFT	16

/*
 * Standard Tx inter-packet gap value recommended for 2.5G operation.
 */
#define IGC_REG_TIPG_DEFAULT	0x00702008

#endif /* __IGC_REG_H */
