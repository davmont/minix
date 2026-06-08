/*
 * nvme.c - NVM Express (PCIe) storage driver for MINIX 3.
 *
 * Phase 1: a single controller, a single namespace (NSID 1), one admin queue
 * pair and one I/O queue pair, polled completion, and a contiguous bounce
 * buffer for DMA (so PRP construction stays trivial).  This is deliberately
 * simple and correct; zero-copy PRPs and MSI-X / multi-queue come later.
 *
 * Modelled on the AHCI and virtio_blk drivers: it is a libblockdriver
 * block device that the boot ramdisk / RS starts for a PCI device of class
 * 01/08/02 (mass storage / NVM / NVMe).
 */

#include <minix/drivers.h>
#include <minix/blockdriver.h>
#include <minix/drvlib.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>
#include <minix/vm.h>
#include <machine/pci.h>
#include <sys/mman.h>
#include <assert.h>

#include "nvme.h"

/* Driver tuning. */
#define NVME_QDEPTH	64			/* entries per queue (admin + I/O) */
#define NVME_IO_QID	1			/* the single I/O queue's id */
#define NVME_NSID	1			/* the namespace we expose */
#define NVME_BOUNCE_PAGES 64			/* 256 KB max per NVMe command */
#define NVME_BOUNCE_SIZE  (NVME_BOUNCE_PAGES * NVME_PAGE_SIZE)
#define NVME_RDY_TIMEOUT  5000			/* ms to wait for CSTS.RDY */

/* A submission/completion queue pair. */
struct nvme_queue {
	nvme_sqe_t	*sq;		/* submission queue (virtual) */
	phys_bytes	sq_phys;
	nvme_cqe_t	*cq;		/* completion queue (virtual) */
	phys_bytes	cq_phys;
	u16_t		qid;
	u16_t		size;		/* number of entries */
	u16_t		sq_tail;
	u16_t		cq_head;
	u8_t		cq_phase;	/* phase tag we currently expect */
};

/* Global controller state. */
static int nvme_devind;			/* PCI device index */
static int nvme_irq;
static int nvme_hook_id;
static volatile char *nvme_regs;	/* mapped BAR0 */
static u32_t nvme_dstrd;			/* doorbell stride (in dwords) */
static u16_t nvme_cid;			/* rolling command identifier */

static struct nvme_queue admin_q;
static struct nvme_queue io_q;

/* Identify buffer (4 KB) + DMA bounce buffer + PRP-list page. */
static u8_t *id_buf;
static phys_bytes id_phys;
static u8_t *bounce;
static phys_bytes bounce_phys;
static u64_t *prp_list;
static phys_bytes prp_list_phys;

/* Namespace geometry. */
static u64_t ns_blocks;			/* size in logical blocks */
static unsigned int ns_lba_size;	/* bytes per logical block */

static int nvme_instance;
static int open_count;

/* Partition tables (libblockdriver/drvlib). */
struct device part[DEV_PER_DRIVE];
struct device subpart[SUB_PER_DRIVE];

static int nvme_open(devminor_t minor, int access);
static int nvme_close(devminor_t minor);
static ssize_t nvme_transfer(devminor_t minor, int do_write, u64_t pos,
	endpoint_t endpt, iovec_t *iov, unsigned int count, int flags);
static struct device *nvme_part(devminor_t minor);
static void nvme_geometry(devminor_t minor, struct part_geom *entry);

static struct blockdriver nvme_dtab = {
	.bdr_type	= BLOCKDRIVER_TYPE_DISK,
	.bdr_open	= nvme_open,
	.bdr_close	= nvme_close,
	.bdr_transfer	= nvme_transfer,
	.bdr_part	= nvme_part,
	.bdr_geometry	= nvme_geometry,
};

/*===========================================================================*
 *			register / doorbell access			     *
 *===========================================================================*/
static u32_t reg_read32(unsigned int off)
{
	return *(volatile u32_t *)(nvme_regs + off);
}

static void reg_write32(unsigned int off, u32_t val)
{
	*(volatile u32_t *)(nvme_regs + off) = val;
}

static u64_t reg_read64(unsigned int off)
{
	/* Read as two 32-bit halves; some controllers dislike 64-bit MMIO. */
	return (u64_t)reg_read32(off) | ((u64_t)reg_read32(off + 4) << 32);
}

static void reg_write64(unsigned int off, u64_t val)
{
	reg_write32(off, (u32_t)val);
	reg_write32(off + 4, (u32_t)(val >> 32));
}

/* Doorbell offsets.  Each doorbell is 4 << CAP.DSTRD bytes apart. */
static unsigned int sq_tail_dbl(u16_t qid)
{
	return NVME_REG_SQ0TDBL + (2 * qid) * (4 << nvme_dstrd);
}

static unsigned int cq_head_dbl(u16_t qid)
{
	return NVME_REG_SQ0TDBL + (2 * qid + 1) * (4 << nvme_dstrd);
}

/*===========================================================================*
 *			submit a command and poll for completion	     *
 *===========================================================================*/
static int nvme_submit_sync(struct nvme_queue *q, nvme_sqe_t *sqe, u32_t *cdw0)
{
	nvme_cqe_t *cqe;
	int i, sc;

	/* Assign a command identifier and place the entry. */
	sqe->cdw0 |= ((u32_t)(++nvme_cid) << 16);
	q->sq[q->sq_tail] = *sqe;

	q->sq_tail = (q->sq_tail + 1) % q->size;
	reg_write32(sq_tail_dbl(q->qid), q->sq_tail);

	/* Poll the completion queue head for the matching phase tag. */
	cqe = &q->cq[q->cq_head];
	for (i = 0; i < NVME_RDY_TIMEOUT; i++) {
		if (NVME_CQE_PHASE(cqe) == q->cq_phase)
			break;
		micro_delay(1000);
	}
	if (NVME_CQE_PHASE(cqe) != q->cq_phase) {
		printf("nvme: command timeout (opcode 0x%x)\n",
			sqe->cdw0 & 0xff);
		return EIO;
	}

	sc = NVME_CQE_SC(cqe);
	if (cdw0 != NULL)
		*cdw0 = cqe->cdw0;

	/* Advance the CQ head, flipping the phase tag on wrap. */
	q->cq_head = (q->cq_head + 1) % q->size;
	if (q->cq_head == 0)
		q->cq_phase ^= 1;
	reg_write32(cq_head_dbl(q->qid), q->cq_head);

	if (sc != 0) {
		printf("nvme: command failed (opcode 0x%x, status 0x%x)\n",
			sqe->cdw0 & 0xff, sc);
		return EIO;
	}
	return OK;
}

/*===========================================================================*
 *			queue allocation				     *
 *===========================================================================*/
static int alloc_queue(struct nvme_queue *q, u16_t qid, u16_t size)
{
	size_t sq_bytes = (size_t)size * NVME_SQE_SIZE;
	size_t cq_bytes = (size_t)size * NVME_CQE_SIZE;

	memset(q, 0, sizeof(*q));
	q->qid = qid;
	q->size = size;
	q->cq_phase = 1;	/* controller starts by writing phase 1 */

	q->sq = alloc_contig(sq_bytes, AC_ALIGN4K, &q->sq_phys);
	if (q->sq == NULL)
		return ENOMEM;
	q->cq = alloc_contig(cq_bytes, AC_ALIGN4K, &q->cq_phys);
	if (q->cq == NULL) {
		free_contig(q->sq, sq_bytes);
		return ENOMEM;
	}
	memset(q->sq, 0, sq_bytes);
	memset(q->cq, 0, cq_bytes);
	return OK;
}

/*===========================================================================*
 *			controller initialisation			     *
 *===========================================================================*/
static int nvme_probe(int skip)
{
	int r, devind;
	u16_t vid, did;

	pci_init();

	r = pci_first_dev(&devind, &vid, &did);
	if (r <= 0)
		return -1;

	while (skip--) {
		r = pci_next_dev(&devind, &vid, &did);
		if (r <= 0)
			return -1;
	}

	pci_reserve(devind);
	return devind;
}

static int nvme_wait_ready(int want)
{
	int i;

	for (i = 0; i < NVME_RDY_TIMEOUT; i++) {
		u32_t csts = reg_read32(NVME_REG_CSTS);
		if (csts & NVME_CSTS_CFS)
			return EIO;
		if (!!(csts & NVME_CSTS_RDY) == want)
			return OK;
		micro_delay(1000);
	}
	return EIO;
}

static int nvme_create_io_queues(void)
{
	nvme_sqe_t cmd;

	/* Create the I/O completion queue (admin opcode 0x05). */
	memset(&cmd, 0, sizeof(cmd));
	cmd.cdw0 = NVME_ADMIN_CREATE_CQ;
	cmd.prp1 = io_q.cq_phys;
	cmd.cdw10 = ((u32_t)(io_q.size - 1) << 16) | io_q.qid;
	cmd.cdw11 = NVME_Q_PC;	/* physically contiguous, interrupts off */
	if (nvme_submit_sync(&admin_q, &cmd, NULL) != OK)
		return EIO;

	/* Create the I/O submission queue (admin opcode 0x01). */
	memset(&cmd, 0, sizeof(cmd));
	cmd.cdw0 = NVME_ADMIN_CREATE_SQ;
	cmd.prp1 = io_q.sq_phys;
	cmd.cdw10 = ((u32_t)(io_q.size - 1) << 16) | io_q.qid;
	cmd.cdw11 = ((u32_t)io_q.qid << 16) | NVME_Q_PC;
	if (nvme_submit_sync(&admin_q, &cmd, NULL) != OK)
		return EIO;

	return OK;
}

static int nvme_identify_namespace(void)
{
	nvme_sqe_t cmd;
	u32_t flbas, lbaf, lbads;

	memset(&cmd, 0, sizeof(cmd));
	cmd.cdw0 = NVME_ADMIN_IDENTIFY;
	cmd.nsid = NVME_NSID;
	cmd.prp1 = id_phys;
	cmd.cdw10 = NVME_CNS_NAMESPACE;
	if (nvme_submit_sync(&admin_q, &cmd, NULL) != OK)
		return EIO;

	/* NSZE (namespace size in blocks) is the first 8 bytes. */
	memcpy(&ns_blocks, &id_buf[0], sizeof(ns_blocks));

	/* FLBAS[3:0] selects the current LBA format; the formats are an array
	 * of 4-byte entries starting at offset 128, with LBADS (log2 of the
	 * block size in bytes) in bits 16..23 of each entry.
	 */
	flbas = id_buf[26] & 0xf;
	memcpy(&lbaf, &id_buf[128 + flbas * 4], sizeof(lbaf));
	lbads = (lbaf >> 16) & 0xff;
	if (lbads < 9 || lbads > 16) {
		printf("nvme: unsupported LBA size exponent %u\n", lbads);
		return EINVAL;
	}
	ns_lba_size = 1U << lbads;

	return OK;
}

static int nvme_init(int devind)
{
	u32_t base, size, cc;
	u64_t cap;
	int r, ioflag;
	u16_t cr;

	/* Map BAR0 (the controller register area) into our address space. */
	if ((r = pci_get_bar(devind, PCI_BAR, &base, &size, &ioflag)) != OK) {
		printf("nvme: unable to retrieve BAR0: %d\n", r);
		return r;
	}
	if (ioflag) {
		printf("nvme: BAR0 is not memory-mapped\n");
		return EINVAL;
	}
	nvme_regs = vm_map_phys(SELF, (void *)(uintptr_t)base, size);
	if (nvme_regs == MAP_FAILED) {
		printf("nvme: unable to map controller registers\n");
		return ENOMEM;
	}

	/* Enable bus mastering and memory space (the PCI server does not do
	 * this for ordinary devices).
	 */
	cr = pci_attr_r16(devind, PCI_CR);
	pci_attr_w16(devind, PCI_CR, cr | PCI_CR_MAST_EN | PCI_CR_MEM_EN);

	/* Register the legacy (INTx) interrupt.  We do not enable controller
	 * interrupt generation in Phase 1 (we poll), but reserving the IRQ
	 * keeps the line owned by us and ready for the interrupt-driven phase.
	 */
	nvme_irq = pci_attr_r8(devind, PCI_ILR);
	nvme_hook_id = 0;
	if ((r = sys_irqsetpolicy(nvme_irq, 0, &nvme_hook_id)) != OK)
		printf("nvme: warning: unable to register IRQ %d: %d\n",
			nvme_irq, r);

	cap = reg_read64(NVME_REG_CAP);
	nvme_dstrd = NVME_CAP_DSTRD(cap);

	if (NVME_CAP_MQES(cap) < NVME_QDEPTH) {
		printf("nvme: controller queue depth %u too small\n",
			(unsigned int)NVME_CAP_MQES(cap));
		return EINVAL;
	}

	/* Disable the controller before (re)configuring it. */
	cc = reg_read32(NVME_REG_CC);
	if (cc & NVME_CC_EN) {
		reg_write32(NVME_REG_CC, cc & ~NVME_CC_EN);
		if (nvme_wait_ready(0) != OK) {
			printf("nvme: controller did not become idle\n");
			return EIO;
		}
	}

	/* Allocate the admin and I/O queues and the DMA helper buffers. */
	if (alloc_queue(&admin_q, 0, NVME_QDEPTH) != OK)
		return ENOMEM;
	if (alloc_queue(&io_q, NVME_IO_QID, NVME_QDEPTH) != OK)
		return ENOMEM;

	id_buf = alloc_contig(NVME_PAGE_SIZE, AC_ALIGN4K, &id_phys);
	bounce = alloc_contig(NVME_BOUNCE_SIZE, AC_ALIGN4K, &bounce_phys);
	prp_list = alloc_contig(NVME_PAGE_SIZE, AC_ALIGN4K, &prp_list_phys);
	if (id_buf == NULL || bounce == NULL || prp_list == NULL) {
		printf("nvme: unable to allocate DMA buffers\n");
		return ENOMEM;
	}

	/* Program the admin queue registers. */
	reg_write32(NVME_REG_AQA,
		((u32_t)(admin_q.size - 1) << 16) | (admin_q.size - 1));
	reg_write64(NVME_REG_ASQ, admin_q.sq_phys);
	reg_write64(NVME_REG_ACQ, admin_q.cq_phys);

	/* Enable the controller: NVM command set, 4 KB pages, round-robin,
	 * 64-byte SQ entries, 16-byte CQ entries.
	 */
	cc = NVME_CC_CSS_NVM | NVME_CC_AMS_RR | NVME_CC_SHN_NONE |
		(0 << NVME_CC_MPS_SHIFT) |
		(NVME_SQES_LOG2 << NVME_CC_IOSQES_SHIFT) |
		(NVME_CQES_LOG2 << NVME_CC_IOCQES_SHIFT) |
		NVME_CC_EN;
	reg_write32(NVME_REG_CC, cc);

	if (nvme_wait_ready(1) != OK) {
		printf("nvme: controller did not become ready\n");
		return EIO;
	}

	/* Bring up the I/O queue pair and learn the namespace geometry. */
	if ((r = nvme_create_io_queues()) != OK)
		return r;
	if ((r = nvme_identify_namespace()) != OK)
		return r;

	printf("nvme%d: %llu blocks of %u bytes (%llu MB), IRQ %d\n",
		nvme_instance, (unsigned long long)ns_blocks, ns_lba_size,
		(unsigned long long)(ns_blocks * ns_lba_size) >> 20, nvme_irq);

	return OK;
}

/*===========================================================================*
 *			a single NVMe read/write command		     *
 *===========================================================================*/
static int nvme_rw(int do_write, u64_t slba, u32_t nblocks)
{
	nvme_sqe_t cmd;
	size_t nbytes = (size_t)nblocks * ns_lba_size;
	unsigned int npages, i;

	memset(&cmd, 0, sizeof(cmd));
	cmd.cdw0 = do_write ? NVME_NVM_WRITE : NVME_NVM_READ;
	cmd.nsid = NVME_NSID;
	cmd.prp1 = bounce_phys;

	/* PRP2 depends on the transfer length.  The bounce buffer is page
	 * aligned, so PRP1 always starts at a page boundary.
	 */
	npages = (nbytes + NVME_PAGE_SIZE - 1) / NVME_PAGE_SIZE;
	if (npages <= 1) {
		cmd.prp2 = 0;
	} else if (npages == 2) {
		cmd.prp2 = bounce_phys + NVME_PAGE_SIZE;
	} else {
		for (i = 1; i < npages; i++)
			prp_list[i - 1] = bounce_phys + (u64_t)i * NVME_PAGE_SIZE;
		cmd.prp2 = prp_list_phys;
	}

	cmd.cdw10 = (u32_t)slba;
	cmd.cdw11 = (u32_t)(slba >> 32);
	cmd.cdw12 = nblocks - 1;	/* NLB is zero-based */

	return nvme_submit_sync(&io_q, &cmd, NULL);
}

/*===========================================================================*
 *			copy between the iovec and the bounce buffer	     *
 *===========================================================================*/
/*
 * Move `len` bytes between the contiguous bounce buffer and the caller's I/O
 * vector, starting `skip` bytes into the vector.  The vector entries are "safe"
 * (grant-based) buffers; sys_vumap resolves them to physical addresses --
 * handling every grant form the block protocol uses, which plain sys_safecopy
 * does not -- and sys_abscopy then moves the data physically.
 */
static int bounce_copy(endpoint_t endpt, iovec_s_t *iov, unsigned int count,
	size_t skip, size_t len, int to_bounce)
{
	struct vumap_vir vir[NR_IOREQS];
	struct vumap_phys phys[NR_IOREQS];
	unsigned int i, nvir, nphys;
	size_t boff = 0;
	int r, access;

	if (count > NR_IOREQS)
		return EINVAL;
	for (i = 0; i < count; i++) {
		vir[i].vv_grant = iov[i].iov_grant;
		vir[i].vv_size = iov[i].iov_size;
	}
	nvir = count;

	/* For a disk write we read the user's buffer; for a read we write it. */
	access = to_bounce ? VUA_READ : VUA_WRITE;
	nphys = NR_IOREQS;
	if ((r = sys_vumap(endpt, vir, nvir, skip, access, phys, &nphys)) != OK)
		return r;

	for (i = 0; i < nphys && len > 0; i++) {
		size_t n = phys[i].vp_size;
		if (n > len)
			n = len;
		if (to_bounce)
			r = sys_abscopy(phys[i].vp_addr, bounce_phys + boff, n);
		else
			r = sys_abscopy(bounce_phys + boff, phys[i].vp_addr, n);
		if (r != OK)
			return r;
		boff += n;
		len -= n;
	}

	return (len == 0) ? OK : EINVAL;
}

/*===========================================================================*
 *			blockdriver callbacks				     *
 *===========================================================================*/
static struct device *nvme_part(devminor_t minor)
{
	if (minor >= 0 && minor < DEV_PER_DRIVE)
		return &part[minor];

	if (minor >= MINOR_d0p0s0) {
		minor -= MINOR_d0p0s0;
		if (minor >= SUB_PER_DRIVE)
			return NULL;
		return &subpart[minor];
	}

	return NULL;
}

static int nvme_open(devminor_t minor, int access)
{
	struct device *dev = nvme_part(minor);

	if (dev == NULL)
		return ENXIO;

	if (open_count == 0) {
		memset(part, 0, sizeof(part));
		memset(subpart, 0, sizeof(subpart));
		part[0].dv_size = ns_blocks * ns_lba_size;
		partition(&nvme_dtab, 0, P_PRIMARY, 0 /* not ATAPI */);
	}

	open_count++;
	return OK;
}

static int nvme_close(devminor_t minor)
{
	if (open_count == 0)
		return EINVAL;
	open_count--;
	return OK;
}

static void nvme_geometry(devminor_t minor, struct part_geom *entry)
{
	/* Report a synthetic geometry; nothing relies on the real values. */
	entry->cylinders = (unsigned long)(ns_blocks / (64 * 32));
	entry->heads = 64;
	entry->sectors = 32;
}

static ssize_t nvme_transfer(devminor_t minor, int do_write, u64_t pos,
	endpoint_t endpt, iovec_t *iov, unsigned int count, int flags)
{
	/* libblockdriver hands us a "safe" (grant-based) I/O vector. */
	iovec_s_t *iv = (iovec_s_t *)iov;
	struct device *dev;
	u64_t disk_pos, end;
	size_t total, done;
	unsigned int i;

	dev = nvme_part(minor);
	if (dev == NULL)
		return ENXIO;

	/* Sum the iovec and validate alignment against the block size. */
	total = 0;
	for (i = 0; i < count; i++)
		total += iv[i].iov_size;
	if (total == 0)
		return 0;

	if ((pos % ns_lba_size) != 0 || (total % ns_lba_size) != 0)
		return EINVAL;

	/* Clip the request to the partition.  A read past the end returns the
	 * bytes that fit; this mirrors the other block drivers.
	 */
	if (pos >= dev->dv_size)
		return 0;
	end = dev->dv_size - pos;
	if (total > end)
		total = (size_t)(end - (end % ns_lba_size));
	if (total == 0)
		return 0;

	disk_pos = dev->dv_base + pos;

	done = 0;
	while (done < total) {
		size_t chunk = total - done;
		u64_t slba;
		u32_t nblk;
		int r;

		if (chunk > NVME_BOUNCE_SIZE)
			chunk = NVME_BOUNCE_SIZE;

		slba = (disk_pos + done) / ns_lba_size;
		nblk = chunk / ns_lba_size;

		if (do_write) {
			r = bounce_copy(endpt, iv, count, done, chunk,
				TRUE /* into bounce */);
			if (r != OK)
				return (done > 0) ? (ssize_t)done : r;
		}

		r = nvme_rw(do_write, slba, nblk);
		if (r != OK)
			return (done > 0) ? (ssize_t)done : EIO;

		if (!do_write) {
			r = bounce_copy(endpt, iv, count, done, chunk,
				FALSE /* out of bounce */);
			if (r != OK)
				return (done > 0) ? (ssize_t)done : r;
		}

		done += chunk;
	}

	return (ssize_t)done;
}

/*===========================================================================*
 *			startup						     *
 *===========================================================================*/
static int sef_cb_init_fresh(int type, sef_init_info_t *UNUSED(info))
{
	long v;
	int devind, r;

	v = 0;
	(void)env_parse("instance", "d", 0, &v, 0, 255);
	nvme_instance = (int)v;

	devind = nvme_probe(nvme_instance);
	if (devind < 0) {
		printf("nvme: no matching NVMe controller found; exiting\n");
		return ENODEV;
	}
	nvme_devind = devind;

	if ((r = nvme_init(devind)) != OK)
		return r;

	blockdriver_announce(type);
	return OK;
}

static void sef_local_startup(void)
{
	sef_setcb_init_fresh(sef_cb_init_fresh);
	sef_startup();
}

int main(int argc, char **argv)
{
	env_setargs(argc, argv);
	sef_local_startup();

	blockdriver_task(&nvme_dtab);

	return OK;
}
