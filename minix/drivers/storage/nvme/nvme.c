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
#include <minix/driver.h>
#include <minix/drvlib.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>
#include <minix/endpoint.h>
#include <minix/vm.h>
#include <machine/pci.h>
#include <sys/mman.h>
#include <assert.h>

#include "nvme.h"

/* Driver tuning. */
#define NVME_QDEPTH	64			/* entries per queue (admin + I/O) */
#define NVME_IO_QID	1			/* the single I/O queue's id */
#define NVME_NSID	1			/* the namespace we expose */
#define NVME_MAX_PAGES	64			/* max pages (== PRP entries) per cmd */
#define NVME_MAX_XFER	(NVME_MAX_PAGES * NVME_PAGE_SIZE)  /* 256 KB per command */
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
static int nvme_hook_id;			/* kernel IRQ hook id */
static volatile char *nvme_regs;	/* mapped BAR0 */
static u32_t nvme_dstrd;			/* doorbell stride (in dwords) */
static u16_t nvme_cid;			/* rolling command identifier */

/* MSI-X interrupt state.  When nvme_use_irq is set, I/O completions are awaited
 * on the MSI-X interrupt instead of by polling.  Admin commands during init are
 * always polled (MSI-X is only enabled once init has finished). */
static int nvme_use_irq;		/* I/O is interrupt-driven */
static u8_t nvme_msix_cap;		/* MSI-X capability config offset */
static volatile u8_t *nvme_msix_table;	/* mapped MSI-X table BAR */

static struct nvme_queue admin_q;
static struct nvme_queue io_q;

/* Identify buffer (4 KB) + PRP-list page (zero-copy DMA goes straight to the
 * caller's pages, so there is no bounce buffer). */
static u8_t *id_buf;
static phys_bytes id_phys;
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
 *			MSI-X interrupt setup				     *
 *===========================================================================*/
/* Walk the PCI capability list and return the config-space offset of the
 * capability with the given id, or 0 if absent. */
static u8_t find_capability(int devind, u8_t cap_id)
{
	u16_t sr;
	u8_t ptr;
	int guard;

	sr = pci_attr_r16(devind, PCI_SR);
	if (!(sr & PSR_CAPPTR))
		return 0;

	ptr = pci_attr_r8(devind, PCI_CAPPTR) & PCI_CP_MASK;
	for (guard = 0; ptr != 0 && guard < 48; guard++) {
		if (pci_attr_r8(devind, ptr + CAP_TYPE) == cap_id)
			return ptr;
		ptr = pci_attr_r8(devind, ptr + CAP_NEXT) & PCI_CP_MASK;
	}
	return 0;
}

/*
 * Find the MSI-X capability, map its table BAR, allocate one MSI vector from
 * the kernel, and program table entry 0 with the kernel-supplied (address,
 * data) pair so the controller's vector 0 raises that interrupt.  The MSI-X
 * Enable bit is *not* set here (see nvme_enable_msix), so no interrupt is
 * delivered until init has finished polling the admin queue.  Returns OK, or
 * an error if MSI-X is unavailable (the caller then falls back to polling).
 */
static int nvme_setup_msix(int devind)
{
	u8_t cap;
	u32_t tbl, base, size, toff;
	int bir, r, ioflag, hook_id;
	u32_t msi_addr, msi_data;
	volatile u8_t *entry;

	cap = find_capability(devind, PCI_CAP_MSIX);
	if (cap == 0)
		return ENODEV;

	/* Locate the MSI-X table: a (BAR index, offset) pair. */
	tbl = pci_attr_r32(devind, cap + MSIX_TABLE_OFF);
	bir = tbl & MSIX_TABLE_BIR_MASK;
	toff = tbl & MSIX_TABLE_OFF_MASK;

	if ((r = pci_get_bar(devind, PCI_BAR + bir * 4, &base, &size,
			&ioflag)) != OK) {
		printf("nvme: cannot get MSI-X table BAR %d: %d\n", bir, r);
		return r;
	}
	if (ioflag)
		return EINVAL;

	nvme_msix_table = vm_map_phys(SELF, (void *)(uintptr_t)base, size);
	if (nvme_msix_table == MAP_FAILED) {
		printf("nvme: cannot map MSI-X table BAR\n");
		return ENOMEM;
	}

	/* Ask the kernel for an MSI vector and the message it encodes. */
	hook_id = 0;	/* notify id 0, like the INTx hook before it */
	if ((r = sys_irqsetpolicy_msi(IRQ_REENABLE, &hook_id, &msi_addr,
			&msi_data)) != OK) {
		printf("nvme: sys_irqsetpolicy_msi failed: %d\n", r);
		return r;
	}
	nvme_hook_id = hook_id;

	/* Program table entry 0 and unmask it. */
	entry = nvme_msix_table + toff;
	*(volatile u32_t *)(entry + MSIX_VEC_ADDR_LO) = msi_addr;
	*(volatile u32_t *)(entry + MSIX_VEC_ADDR_HI) = 0;
	*(volatile u32_t *)(entry + MSIX_VEC_DATA) = msi_data;
	*(volatile u32_t *)(entry + MSIX_VEC_CTRL) = 0;

	nvme_msix_cap = cap;
	return OK;
}

/* Set the MSI-X Enable bit (and clear the function mask) once init is done. */
static void nvme_enable_msix(int devind)
{
	u16_t ctrl = pci_attr_r16(devind, nvme_msix_cap + MSIX_CTRL_OFF);

	ctrl |= MSIX_CTRL_ENABLE;
	ctrl &= ~MSIX_CTRL_FUNC_MASK;
	pci_attr_w16(devind, nvme_msix_cap + MSIX_CTRL_OFF, ctrl);
}

/*
 * Block until the I/O completion queue's MSI-X interrupt indicates the next
 * entry is ours (phase tag flipped), or a watchdog alarm fires.  Other
 * messages that arrive while we wait are deferred to the blockdriver loop, as
 * in at_wini's w_intr_wait().  IRQ_REENABLE means the kernel re-arms the hook
 * for us, so no explicit sys_irqenable() is needed here.
 */
static int nvme_wait_irq(struct nvme_queue *q)
{
	nvme_cqe_t *cqe = &q->cq[q->cq_head];
	message m;
	int ipc_status, r;
	long tick, spins;

	/* Re-poll the completion queue periodically rather than blocking on the
	 * MSI-X interrupt forever: under emulation (and on some controllers) a
	 * completion interrupt is occasionally not delivered even though the CQE
	 * has been posted.  A short timer wakes us to re-check the phase tag, so
	 * a lost interrupt costs a few milliseconds instead of failing the
	 * command -- and, crucially, never leaves the CQ head out of sync. */
	tick = sys_hz() / 100;			/* ~10 ms */
	if (tick < 1)
		tick = 1;
	spins = 0;

	while (NVME_CQE_PHASE(cqe) != q->cq_phase) {
		sys_setalarm(tick, 0);

		if ((r = driver_receive(ANY, &m, &ipc_status)) != OK)
			panic("nvme: driver_receive failed: %d", r);

		if (is_ipc_notify(ipc_status)) {
			switch (_ENDPOINT_P(m.m_source)) {
			case HARDWARE:
				/* Our completion interrupt; re-check the CQ. */
				break;
			case CLOCK:
				/* Timer tick: re-poll the CQ.  Give up only
				 * after a long stretch of no progress (~5 s),
				 * which indicates a genuinely wedged device. */
				if (++spins >= 500) {
					sys_setalarm(0, 0);
					printf("nvme: I/O command timed out\n");
					return EIO;
				}
				break;
			default:
				blockdriver_mq_queue(&m, ipc_status);
			}
		} else {
			blockdriver_mq_queue(&m, ipc_status);
		}
	}

	sys_setalarm(0, 0);	/* cancel the watchdog */
	return OK;
}

/*===========================================================================*
 *			submit a command and wait for completion	     *
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

	/* Wait for the completion: interrupt-driven once MSI-X is up (I/O), or
	 * polled (all admin commands, and as a fallback if MSI-X is absent). */
	cqe = &q->cq[q->cq_head];
	if (nvme_use_irq) {
		if (nvme_wait_irq(q) != OK)
			return EIO;
	} else {
		for (i = 0; i < NVME_RDY_TIMEOUT; i++) {
			if (NVME_CQE_PHASE(cqe) == q->cq_phase)
				break;
			micro_delay(1000);
		}
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
	/* Physically contiguous; if MSI-X is up, enable interrupts on this CQ
	 * and route them to vector 0 (DW11[31:16]).  Without MSI-X the IEN bit
	 * is harmless: no interrupt is delivered and completions are polled. */
	cmd.cdw11 = NVME_Q_PC;
	if (nvme_msix_cap != 0)
		cmd.cdw11 |= NVME_CQ_IEN | (0 << NVME_CQ_IV_SHIFT);
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

	/* Allocate and program an MSI-X vector for interrupt-driven I/O
	 * completion.  Do not enable MSI-X yet: init polls the admin queue, so
	 * interrupts are only switched on once that is done.  If the controller
	 * has no MSI-X (or setup fails), fall back to polled completion. */
	if (nvme_setup_msix(devind) != OK) {
		nvme_msix_cap = 0;
		printf("nvme: MSI-X unavailable; using polled completion\n");
	}

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
	prp_list = alloc_contig(NVME_PAGE_SIZE, AC_ALIGN4K, &prp_list_phys);
	if (id_buf == NULL || prp_list == NULL) {
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

	/* Bring up the I/O queue pair and learn the namespace geometry.  These
	 * admin commands are still polled (nvme_use_irq is clear). */
	if ((r = nvme_create_io_queues()) != OK)
		return r;
	if ((r = nvme_identify_namespace()) != OK)
		return r;

	/* All admin work is done: switch I/O completion to MSI-X interrupts. */
	if (nvme_msix_cap != 0) {
		nvme_enable_msix(devind);
		if ((r = sys_irqenable(&nvme_hook_id)) != OK) {
			printf("nvme: unable to enable MSI-X IRQ: %d\n", r);
			return r;
		}
		nvme_use_irq = TRUE;
	}

	printf("nvme%d: %llu blocks of %u bytes (%llu MB), %s\n",
		nvme_instance, (unsigned long long)ns_blocks, ns_lba_size,
		(unsigned long long)(ns_blocks * ns_lba_size) >> 20,
		nvme_use_irq ? "MSI-X" : "polled");

	return OK;
}

/*===========================================================================*
 *			a single NVMe read/write command		     *
 *===========================================================================*/
/*
 * Build the NVMe PRP entries for `nbytes` bytes of a buffer described, in buffer
 * order, by the physical segments phys[0..nphys).  Per the NVMe PRP rules, PRP1
 * may carry a page offset and every later entry must be page-aligned (which
 * holds: page boundaries of a contiguous virtual buffer fall on page-aligned
 * physical addresses).  Pages 2..N are written into the single prp_list page.
 */
static int build_prp(struct vumap_phys *phys, unsigned int nphys, size_t nbytes,
	u64_t *prp1, u64_t *prp2, size_t *covered)
{
	const size_t PS = NVME_PAGE_SIZE;
	size_t off, cov, bpos, seg_start, seg_rem, want;
	unsigned int s, nlist;
	u64_t pa;

	*prp1 = phys[0].vp_addr;
	off = (size_t)(phys[0].vp_addr & (PS - 1));

	/* Bytes contiguously usable through PRP1's first page, bounded by the
	 * extent of segment 0.  NVMe gives PRP1 everything from its offset to the
	 * end of its page, so segment 0 must actually reach that page boundary to
	 * chain further; otherwise this command can only cover segment 0. */
	cov = PS - off;
	if (cov > phys[0].vp_size)
		cov = phys[0].vp_size;
	if (cov > nbytes)
		cov = nbytes;

	if (cov >= nbytes || (off + cov) < PS) {
		/* Single PRP entry suffices (everything fits, or segment 0 ends
		 * before its page boundary so we cannot add a PRP list). */
		*prp2 = 0;
		*covered = cov;
		return OK;
	}

	/* PRP1 fills to its page boundary (cov == PS - off).  Add one PRP-list
	 * entry per subsequent page, stopping at the first page that is not
	 * page-aligned or not fully backed by a single contiguous segment -- the
	 * caller re-vumaps and issues another command for the remainder. */
	nlist = 0;
	while (cov < nbytes) {
		bpos = cov;			/* buffer offset of the next page */

		seg_start = 0;
		for (s = 0; s < nphys; s++) {
			if (seg_start + phys[s].vp_size > bpos)
				break;
			seg_start += phys[s].vp_size;
		}
		if (s >= nphys)
			break;

		pa = phys[s].vp_addr + (bpos - seg_start);
		if (pa & (PS - 1))
			break;			/* page not page-aligned: stop */

		seg_rem = phys[s].vp_size - (bpos - seg_start);
		want = nbytes - cov;
		if (want > PS)
			want = PS;
		if (want == PS && seg_rem < PS)
			break;			/* full page not backed: stop */
		if (want > seg_rem)
			want = seg_rem;		/* final partial page */
		if (nlist >= PS / sizeof(u64_t))
			break;			/* one PRP-list page max */

		prp_list[nlist++] = pa;
		cov += want;
	}

	*prp2 = (nlist == 0) ? 0 : (nlist == 1 ? prp_list[0] : prp_list_phys);
	*covered = cov;
	return OK;
}

/* Issue one NVM read/write command with caller-supplied PRP pointers. */
static int nvme_rw_direct(int do_write, u64_t slba, u32_t nblocks,
	u64_t prp1, u64_t prp2)
{
	nvme_sqe_t cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.cdw0 = do_write ? NVME_NVM_WRITE : NVME_NVM_READ;
	cmd.nsid = NVME_NSID;
	cmd.prp1 = prp1;
	cmd.prp2 = prp2;
	cmd.cdw10 = (u32_t)slba;
	cmd.cdw11 = (u32_t)(slba >> 32);
	cmd.cdw12 = nblocks - 1;	/* NLB is zero-based */

	return nvme_submit_sync(&io_q, &cmd, NULL);
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

	if (count > NR_IOREQS)
		return EINVAL;

	done = 0;
	while (done < total) {
		struct vumap_vir vir[NR_IOREQS];
		struct vumap_phys phys[NR_IOREQS];
		unsigned int nphys;
		size_t chunk = total - done;
		size_t avail, covered;
		u64_t slba, prp1, prp2;
		u32_t nblk;
		int r, access;

		/* Cap each command so the (worst-case page-fragmented) buffer
		 * always fits in NR_IOREQS physical segments and one PRP list. */
		if (chunk > NVME_MAX_XFER)
			chunk = NVME_MAX_XFER;

		/* Resolve this chunk of the caller's buffer to physical segments
		 * and DMA straight to/from them -- no bounce buffer.  For a disk
		 * write we read the user's pages (VUA_READ); for a read we write
		 * them (VUA_WRITE). */
		for (i = 0; i < count; i++) {
			vir[i].vv_grant = iv[i].iov_grant;
			vir[i].vv_size = iv[i].iov_size;
		}
		access = do_write ? VUA_READ : VUA_WRITE;
		nphys = NR_IOREQS;
		if ((r = sys_vumap(endpt, vir, count, done, access, phys,
				&nphys)) != OK)
			return (done > 0) ? (ssize_t)done : r;

		/* vumap may resolve fewer bytes than requested if the buffer is
		 * highly fragmented (it returns at most NR_IOREQS segments).
		 * Clamp this command to what it covered, block-aligned, and let
		 * the loop pick up the rest. */
		avail = 0;
		for (i = 0; i < nphys; i++)
			avail += phys[i].vp_size;
		if (chunk > avail)
			chunk = avail;
		chunk -= chunk % ns_lba_size;
		if (chunk == 0)
			return (done > 0) ? (ssize_t)done : EIO;

		if ((r = build_prp(phys, nphys, chunk, &prp1, &prp2,
				&covered)) != OK)
			return (done > 0) ? (ssize_t)done : r;

		/* build_prp may cover fewer bytes than requested if the buffer is
		 * fragmented into sub-page physical segments (PRP cannot express
		 * those in one command); the loop handles the remainder. */
		covered -= covered % ns_lba_size;
		if (covered == 0)
			return (done > 0) ? (ssize_t)done : EIO;
		chunk = covered;

		slba = (disk_pos + done) / ns_lba_size;
		nblk = chunk / ns_lba_size;

		r = nvme_rw_direct(do_write, slba, nblk, prp1, prp2);
		if (r != OK)
			return (done > 0) ? (ssize_t)done : EIO;

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
