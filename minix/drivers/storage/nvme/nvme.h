/*
 * nvme.h - register, command and structure definitions for the MINIX NVMe
 * driver (NVM Express 1.x, PCIe).  See the NVMe base specification for the
 * authoritative layout.
 */
#ifndef _NVME_H
#define _NVME_H

#include <minix/type.h>

/* Controller registers, as byte offsets into BAR0 (MMIO). */
#define NVME_REG_CAP	0x00	/* Controller Capabilities (64-bit) */
#define NVME_REG_VS	0x08	/* Version (32-bit) */
#define NVME_REG_INTMS	0x0c	/* Interrupt Mask Set (32-bit) */
#define NVME_REG_INTMC	0x10	/* Interrupt Mask Clear (32-bit) */
#define NVME_REG_CC	0x14	/* Controller Configuration (32-bit) */
#define NVME_REG_CSTS	0x1c	/* Controller Status (32-bit) */
#define NVME_REG_AQA	0x24	/* Admin Queue Attributes (32-bit) */
#define NVME_REG_ASQ	0x28	/* Admin Submission Queue base (64-bit) */
#define NVME_REG_ACQ	0x30	/* Admin Completion Queue base (64-bit) */
#define NVME_REG_SQ0TDBL 0x1000	/* Submission Queue 0 Tail Doorbell */

/* CAP (Controller Capabilities) fields. */
#define NVME_CAP_MQES(c)	(((c) & 0xffffULL) + 1)		/* max queue entries */
#define NVME_CAP_TO(c)		(((c) >> 24) & 0xff)		/* timeout, 500ms units */
#define NVME_CAP_DSTRD(c)	(((c) >> 32) & 0xf)		/* doorbell stride */
#define NVME_CAP_MPSMIN(c)	(((c) >> 48) & 0xf)		/* min page size */

/* CC (Controller Configuration) fields. */
#define NVME_CC_EN		(1 << 0)	/* Enable */
#define NVME_CC_CSS_NVM		(0 << 4)	/* NVM command set */
#define NVME_CC_MPS_SHIFT	7		/* Memory Page Size (2^(12+MPS)) */
#define NVME_CC_AMS_RR		(0 << 11)	/* round robin arbitration */
#define NVME_CC_SHN_NONE	(0 << 14)	/* no shutdown notification */
#define NVME_CC_IOSQES_SHIFT	16		/* I/O SQ entry size (2^n) */
#define NVME_CC_IOCQES_SHIFT	20		/* I/O CQ entry size (2^n) */

/* CSTS (Controller Status) fields. */
#define NVME_CSTS_RDY		(1 << 0)	/* Ready */
#define NVME_CSTS_CFS		(1 << 1)	/* Controller Fatal Status */

/* Admin opcodes. */
#define NVME_ADMIN_DELETE_SQ	0x00
#define NVME_ADMIN_CREATE_SQ	0x01
#define NVME_ADMIN_DELETE_CQ	0x04
#define NVME_ADMIN_CREATE_CQ	0x05
#define NVME_ADMIN_IDENTIFY	0x06
#define NVME_ADMIN_SET_FEATURES	0x09

/* NVM (I/O) opcodes. */
#define NVME_NVM_FLUSH		0x00
#define NVME_NVM_WRITE		0x01
#define NVME_NVM_READ		0x02

/* IDENTIFY CNS values. */
#define NVME_CNS_NAMESPACE	0x00
#define NVME_CNS_CONTROLLER	0x01

/* SET_FEATURES feature identifiers. */
#define NVME_FEAT_NUM_QUEUES	0x07

/* CREATE_CQ / CREATE_SQ DW11 flags. */
#define NVME_Q_PC		(1 << 0)	/* physically contiguous */
#define NVME_CQ_IEN		(1 << 1)	/* interrupts enabled */
#define NVME_CQ_IV_SHIFT	16		/* CREATE_CQ DW11: MSI-X vector */

/*
 * PCI MSI-X capability (capability ID 0x11) and table layout.  Offsets named
 * "*_OFF" are relative to the start of the capability in PCI config space;
 * the MSIX_VEC_* offsets are relative to a 16-byte table entry in the mapped
 * table BAR.
 */
#define PCI_CAP_MSIX		0x11
#define MSIX_CTRL_OFF		0x02		/* message control (16-bit) */
#define   MSIX_CTRL_TSIZE_MASK	0x07ff		/* table size minus one */
#define   MSIX_CTRL_FUNC_MASK	(1 << 14)	/* function mask */
#define   MSIX_CTRL_ENABLE	(1 << 15)	/* MSI-X enable */
#define MSIX_TABLE_OFF		0x04		/* table offset / BIR (32-bit) */
#define   MSIX_TABLE_BIR_MASK	0x00000007	/* which BAR holds the table */
#define   MSIX_TABLE_OFF_MASK	0xfffffff8	/* offset of the table in it */

#define MSIX_VEC_SIZE		16		/* bytes per table entry */
#define   MSIX_VEC_ADDR_LO	0x00
#define   MSIX_VEC_ADDR_HI	0x04
#define   MSIX_VEC_DATA		0x08
#define   MSIX_VEC_CTRL		0x0c
#define     MSIX_VEC_CTRL_MASK	(1 << 0)	/* per-vector mask bit */

/* The fixed entry sizes (powers of two) used by NVMe. */
#define NVME_SQE_SIZE		64		/* submission queue entry */
#define NVME_CQE_SIZE		16		/* completion queue entry */
#define NVME_SQES_LOG2		6
#define NVME_CQES_LOG2		4

#define NVME_PAGE_SIZE		4096		/* MPS=0 host page size */

/* A submission queue entry (64 bytes, 16 dwords). */
typedef struct {
	u32_t	cdw0;		/* opcode, fuse, cid */
	u32_t	nsid;
	u32_t	cdw2;
	u32_t	cdw3;
	u64_t	mptr;		/* metadata pointer */
	u64_t	prp1;
	u64_t	prp2;
	u32_t	cdw10;
	u32_t	cdw11;
	u32_t	cdw12;
	u32_t	cdw13;
	u32_t	cdw14;
	u32_t	cdw15;
} nvme_sqe_t;

/* A completion queue entry (16 bytes, 4 dwords). */
typedef struct {
	u32_t	cdw0;		/* command specific */
	u32_t	cdw1;
	u32_t	sq;		/* [15:0] sq head, [31:16] sq id */
	u32_t	status;		/* [15:0] cid, [16] phase, [31:17] status code */
} nvme_cqe_t;

#define NVME_CQE_PHASE(c)	(((c)->status >> 16) & 1)
#define NVME_CQE_CID(c)		((c)->status & 0xffff)
#define NVME_CQE_SC(c)		(((c)->status >> 17) & 0x7fff)	/* status field */
#define NVME_CQE_SQHD(c)	((c)->sq & 0xffff)

#endif /* _NVME_H */
