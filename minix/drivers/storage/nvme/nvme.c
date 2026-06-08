/*
 * nvme.c - NVM Express (PCIe) storage driver for MINIX 3.
 *
 * A single controller and namespace (NSID 1) with one admin queue pair and
 * several I/O queue pairs.  Multithreaded (libblockdriver_mt): each worker
 * thread owns one I/O queue carrying a single in-flight command, so multiple
 * commands are in flight at once.  Completion is interrupt-driven over a single
 * shared MSI-X vector (with a polling backstop timer for lost interrupts), and
 * DMA is zero-copy -- the controller's PRP lists point straight at the caller's
 * pages, resolved via sys_vumap.
 *
 * Modelled on the AHCI and virtio_blk drivers: it is a libblockdriver
 * block device that the boot ramdisk / RS starts for a PCI device of class
 * 01/08/02 (mass storage / NVM / NVMe).
 */

#include <minix/drivers.h>
#include <minix/blockdriver_mt.h>
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
#define NVME_NUM_IO_QUEUES 4			/* I/O queue pairs == worker threads */
#define NVME_NSID	1			/* the namespace we expose */
#define NVME_MAX_PAGES	64			/* max pages (== PRP entries) per cmd */
#define NVME_MAX_XFER	(NVME_MAX_PAGES * NVME_PAGE_SIZE)  /* 256 KB per command */
#define NVME_RDY_TIMEOUT  5000			/* ms to wait for CSTS.RDY */

/* A submission/completion queue pair.  In the multithreaded I/O model each
 * queue carries at most one in-flight command, owned by the worker thread that
 * claimed the queue; the master thread completes it from the interrupt (or the
 * backstop timer) and wakes that worker. */
struct nvme_queue {
	nvme_sqe_t	*sq;		/* submission queue (virtual) */
	phys_bytes	sq_phys;
	nvme_cqe_t	*cq;		/* completion queue (virtual) */
	phys_bytes	cq_phys;
	u16_t		qid;
	u16_t		size;		/* number of entries */
	u16_t		sq_tail;
	u16_t		cq_head;
	u16_t		cid;		/* rolling command identifier */
	u8_t		cq_phase;	/* phase tag we currently expect */
	/* I/O-queue runtime state (unused by the admin queue). */
	int		busy;		/* claimed by a worker thread */
	int		pending;	/* a command awaits completion */
	int		done;		/* completion recorded */
	int		result;		/* OK or EIO, set on completion */
	int		spins;		/* backstop ticks with no progress */
	thread_id_t	waiter;		/* worker thread to wake */
	minix_timer_t	timer;		/* lost-interrupt backstop */
	u64_t		*prp_list;	/* private PRP-list page (DMA) */
	phys_bytes	prp_list_phys;
};

/* Global controller state. */
static int nvme_devind;			/* PCI device index */
static volatile char *nvme_regs;	/* mapped BAR0 */
static u32_t nvme_dstrd;			/* doorbell stride (in dwords) */

/* MSI-X interrupt state.  All I/O queues share a single MSI-X vector (entry 0);
 * controllers commonly expose fewer interrupt vectors than queues, so on each
 * interrupt the handler scans every queue's completion queue.  Admin commands
 * during init are always polled (MSI-X is only enabled once init has finished). */
static int nvme_use_irq;		/* I/O is interrupt-driven */
static u8_t nvme_msix_cap;		/* MSI-X capability config offset */
static volatile u8_t *nvme_msix_table;	/* mapped MSI-X table BAR */
static u32_t nvme_msix_toff;		/* table offset within its BAR */
static int nvme_msix_hook;		/* kernel IRQ hook id (shared vector) */

static struct nvme_queue admin_q;
static struct nvme_queue io_q[NVME_NUM_IO_QUEUES];
static int nvme_nq;			/* I/O queues actually created */
static clock_t nvme_backstop_ticks;	/* lost-interrupt re-poll interval */

/* Identify buffer (4 KB).  Zero-copy DMA goes straight to the caller's pages,
 * so there is no bounce buffer; each I/O queue carries its own PRP-list page. */
static u8_t *id_buf;
static phys_bytes id_phys;

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
static void nvme_intr(unsigned int mask);
static void nvme_alarm(clock_t stamp);
static int nvme_device(devminor_t minor, device_id_t *id);

static struct blockdriver nvme_dtab = {
	.bdr_type	= BLOCKDRIVER_TYPE_DISK,
	.bdr_open	= nvme_open,
	.bdr_close	= nvme_close,
	.bdr_transfer	= nvme_transfer,
	.bdr_part	= nvme_part,
	.bdr_geometry	= nvme_geometry,
	.bdr_intr	= nvme_intr,
	.bdr_alarm	= nvme_alarm,
	.bdr_device	= nvme_device,
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
 * Find the MSI-X capability and map its table BAR.  This only locates and maps
 * the table; the shared vector is allocated and programmed later by
 * nvme_msix_program().  The MSI-X Enable bit is set separately
 * (nvme_enable_msix), after init has finished polling the admin queue.  Returns
 * OK, or an error if MSI-X is unavailable (the caller then falls back to
 * timer-driven completion).
 */
static int nvme_setup_msix(int devind)
{
	u8_t cap;
	u32_t tbl, base, size;
	int bir, r, ioflag;

	cap = find_capability(devind, PCI_CAP_MSIX);
	if (cap == 0)
		return ENODEV;

	/* Locate the MSI-X table: a (BAR index, offset) pair. */
	tbl = pci_attr_r32(devind, cap + MSIX_TABLE_OFF);
	bir = tbl & MSIX_TABLE_BIR_MASK;
	nvme_msix_toff = tbl & MSIX_TABLE_OFF_MASK;

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

	nvme_msix_cap = cap;
	return OK;
}

/*
 * Allocate the single shared MSI vector (notify-id 0) and program MSI-X table
 * entry 0 with the kernel's (address, data) pair.  All I/O queues raise this one
 * vector; the handler scans every queue.  Records the hook id for sys_irqenable.
 */
static int nvme_msix_program(void)
{
	volatile u8_t *entry;
	u32_t msi_addr, msi_data;
	int hook_id, r;

	hook_id = 0;	/* notify-id 0 */
	if ((r = sys_irqsetpolicy_msi(IRQ_REENABLE, &hook_id, &msi_addr,
			&msi_data)) != OK) {
		printf("nvme: sys_irqsetpolicy_msi failed: %d\n", r);
		return r;
	}
	nvme_msix_hook = hook_id;

	entry = nvme_msix_table + nvme_msix_toff;	/* table entry 0 */
	*(volatile u32_t *)(entry + MSIX_VEC_ADDR_LO) = msi_addr;
	*(volatile u32_t *)(entry + MSIX_VEC_ADDR_HI) = 0;
	*(volatile u32_t *)(entry + MSIX_VEC_DATA) = msi_data;
	*(volatile u32_t *)(entry + MSIX_VEC_CTRL) = 0;	/* unmask */

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

/*===========================================================================*
 *			admin command: submit and poll			     *
 *===========================================================================*/
/*
 * Submit one command on `q` and poll its completion queue for the result.  Used
 * only for admin commands during initialisation, which is single-threaded and
 * runs before MSI-X is enabled.  I/O commands use the interrupt-driven path
 * (nvme_io_cmd) instead.
 */
static int nvme_submit_sync(struct nvme_queue *q, nvme_sqe_t *sqe, u32_t *cdw0)
{
	nvme_cqe_t *cqe;
	int i, sc;

	/* Assign a command identifier and place the entry. */
	sqe->cdw0 |= ((u32_t)(++q->cid) << 16);
	q->sq[q->sq_tail] = *sqe;

	q->sq_tail = (q->sq_tail + 1) % q->size;
	reg_write32(sq_tail_dbl(q->qid), q->sq_tail);

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
 *		interrupt-driven I/O completion (multithreaded)		     *
 *===========================================================================*/
/*
 * Consume the single in-flight completion on an I/O queue (if posted), advance
 * the CQ head, and wake the worker thread that issued it.  Runs on the master
 * thread, from the interrupt handler or the backstop timer.
 */
static void nvme_drain_cq(struct nvme_queue *q)
{
	nvme_cqe_t *cqe = &q->cq[q->cq_head];
	int sc;

	if (NVME_CQE_PHASE(cqe) != q->cq_phase)
		return;				/* nothing new */

	sc = NVME_CQE_SC(cqe);

	q->cq_head = (q->cq_head + 1) % q->size;
	if (q->cq_head == 0)
		q->cq_phase ^= 1;
	reg_write32(cq_head_dbl(q->qid), q->cq_head);

	if (q->pending) {
		cancel_timer(&q->timer);
		q->result = (sc != 0) ? EIO : OK;
		q->pending = 0;
		q->done = 1;
		blockdriver_mt_wakeup(q->waiter);
	}
}

/* MSI-X interrupt.  All I/O queues share one vector, so scan every queue's
 * completion queue and complete whatever is ready. */
static void nvme_intr(unsigned int mask)
{
	int i;

	(void)mask;
	for (i = 0; i < nvme_nq; i++)
		nvme_drain_cq(&io_q[i]);
}

/* libblockdriver alarm dispatch -> fire any due timers. */
static void nvme_alarm(clock_t stamp)
{
	expire_timers(stamp);
}

/*
 * Backstop timer for a queue whose completion interrupt never arrived (lost
 * under emulation).  Re-poll the CQ; if still outstanding, re-arm, and fail the
 * command only after a long stretch (~5 s) that indicates a wedged device.
 */
static void nvme_timeout(int arg)
{
	struct nvme_queue *q = &io_q[arg];

	if (!q->pending)
		return;

	nvme_drain_cq(q);			/* maybe the interrupt was lost */
	if (!q->pending)
		return;				/* completed after all */

	if (++q->spins >= 500) {		/* ~5 s of no progress */
		printf("nvme: I/O command timed out on queue %d\n", q->qid);
		q->result = EIO;
		q->pending = 0;
		q->done = 1;
		blockdriver_mt_wakeup(q->waiter);
		return;
	}

	set_timer(&q->timer, nvme_backstop_ticks, nvme_timeout, arg);
}

/*
 * Claim a free I/O queue for the calling worker thread.  Each queue carries one
 * in-flight command and its own PRP-list page.  This runs without yielding from
 * the scan until the sleep in nvme_submit_io(), so the scan-and-mark is atomic
 * against the other (cooperative) workers; with one queue per worker thread a
 * free queue always exists.
 */
static struct nvme_queue *nvme_claim_queue(void)
{
	int i;

	for (i = 0; i < nvme_nq; i++) {
		if (!io_q[i].busy) {
			io_q[i].busy = 1;
			return &io_q[i];
		}
	}

	io_q[0].busy = 1;	/* defensive: should never be reached */
	return &io_q[0];
}

static void nvme_release_queue(struct nvme_queue *q)
{
	q->busy = 0;
}

/*
 * Submit one command on a claimed queue and block the calling worker thread
 * until the master thread (interrupt handler or backstop timer) completes it.
 */
static int nvme_submit_io(struct nvme_queue *q, nvme_sqe_t *sqe)
{
	sqe->cdw0 |= ((u32_t)(++q->cid) << 16);
	q->sq[q->sq_tail] = *sqe;
	q->sq_tail = (q->sq_tail + 1) % q->size;

	q->done = 0;
	q->result = OK;
	q->spins = 0;
	q->pending = 1;
	q->waiter = blockdriver_mt_get_tid();
	set_timer(&q->timer, nvme_backstop_ticks, nvme_timeout,
		(int)(q - io_q));

	reg_write32(sq_tail_dbl(q->qid), q->sq_tail);	/* ring after SQE write */

	while (!q->done)
		blockdriver_mt_sleep();

	return q->result;
}

/*===========================================================================*
 *			queue allocation				     *
 *===========================================================================*/
static int alloc_queue(struct nvme_queue *q, u16_t qid, u16_t size)
{
	size_t sq_bytes = (size_t)size * NVME_SQE_SIZE;
	size_t cq_bytes = (size_t)size * NVME_CQE_SIZE;
	const size_t PS = NVME_PAGE_SIZE;
	size_t blk_bytes = 3 * PS;	/* sq page, cq page, prp-list page */
	char *blk;
	phys_bytes blk_phys;

	/* The submission and completion queues must each fit in one page. */
	assert(sq_bytes <= PS && cq_bytes <= PS);

	memset(q, 0, sizeof(*q));
	q->qid = qid;
	q->size = size;
	q->cq_phase = 1;	/* controller starts by writing phase 1 */

	/* One contiguous, page-aligned DMA block per queue carved into three
	 * page-aligned regions: SQ, CQ, and a private PRP-list page (the latter
	 * so concurrent commands on different queues never clobber each other's
	 * descriptor list).  A single allocation keeps the number of distinct
	 * DMA mappings low. */
	blk = alloc_contig(blk_bytes, AC_ALIGN4K, &blk_phys);
	if (blk == NULL)
		return ENOMEM;
	memset(blk, 0, blk_bytes);

	q->sq = (nvme_sqe_t *)(blk + 0 * PS);
	q->sq_phys = blk_phys + 0 * PS;
	q->cq = (nvme_cqe_t *)(blk + 1 * PS);
	q->cq_phys = blk_phys + 1 * PS;
	q->prp_list = (u64_t *)(blk + 2 * PS);
	q->prp_list_phys = blk_phys + 2 * PS;

	init_timer(&q->timer);
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

/*
 * Ask the controller (Set Features, Number of Queues) to allocate `want` I/O
 * queue pairs.  Returns, in *granted, the number it actually allocated (the
 * smaller of the submission and completion allocations), which may be less.
 */
static int nvme_set_num_queues(int want, int *granted)
{
	nvme_sqe_t cmd;
	u32_t cdw0;
	int nsqa, ncqa;

	memset(&cmd, 0, sizeof(cmd));
	cmd.cdw0 = NVME_ADMIN_SET_FEATURES;
	cmd.cdw10 = NVME_FEAT_NUM_QUEUES;
	/* NSQR/NCQR are zero-based counts of *requested* queues. */
	cmd.cdw11 = ((u32_t)(want - 1) << 16) | (u32_t)(want - 1);
	if (nvme_submit_sync(&admin_q, &cmd, &cdw0) != OK)
		return EIO;

	/* DW0: [15:0]=NSQA, [31:16]=NCQA (zero-based *allocated* counts). */
	nsqa = (cdw0 & 0xffff) + 1;
	ncqa = ((cdw0 >> 16) & 0xffff) + 1;
	*granted = (nsqa < ncqa) ? nsqa : ncqa;
	return OK;
}

static int nvme_create_io_queues(void)
{
	nvme_sqe_t cmd;
	int k;

	for (k = 0; k < nvme_nq; k++) {
		struct nvme_queue *q = &io_q[k];

		/* Create the I/O completion queue (admin opcode 0x05).  All I/O
		 * CQs share a single MSI-X vector (0): controllers commonly
		 * support far fewer interrupt vectors than queues, so on each
		 * interrupt the handler scans every queue's CQ.  Concurrency still
		 * comes from having one in-flight command per queue. */
		memset(&cmd, 0, sizeof(cmd));
		cmd.cdw0 = NVME_ADMIN_CREATE_CQ;
		cmd.prp1 = q->cq_phys;
		cmd.cdw10 = ((u32_t)(q->size - 1) << 16) | q->qid;
		cmd.cdw11 = NVME_Q_PC;
		if (nvme_msix_cap != 0)
			cmd.cdw11 |= NVME_CQ_IEN | (0 << NVME_CQ_IV_SHIFT);
		if (nvme_submit_sync(&admin_q, &cmd, NULL) != OK) {
			return EIO;
		}

		/* Create the matching I/O submission queue (opcode 0x01); its
		 * completions post to the CQ with the same id (DW11[31:16]). */
		memset(&cmd, 0, sizeof(cmd));
		cmd.cdw0 = NVME_ADMIN_CREATE_SQ;
		cmd.prp1 = q->sq_phys;
		cmd.cdw10 = ((u32_t)(q->size - 1) << 16) | q->qid;
		cmd.cdw11 = ((u32_t)q->qid << 16) | NVME_Q_PC;
		if (nvme_submit_sync(&admin_q, &cmd, NULL) != OK) {
			return EIO;
		}
	}

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
	int r, ioflag, i;
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

	/* Allocate the admin queue and DMA helper buffers; the I/O queues come
	 * later, once the controller tells us how many pairs it will grant. */
	if (alloc_queue(&admin_q, 0, NVME_QDEPTH) != OK)
		return ENOMEM;

	id_buf = alloc_contig(NVME_PAGE_SIZE, AC_ALIGN4K, &id_phys);
	if (id_buf == NULL) {
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

	/* Negotiate the number of I/O queue pairs: cap our fixed maximum to what
	 * the controller grants. */
	nvme_nq = NVME_NUM_IO_QUEUES;
	{
		int granted = 1;

		if ((r = nvme_set_num_queues(nvme_nq, &granted)) != OK)
			return r;
		if (granted < nvme_nq)
			nvme_nq = granted;
	}
	if (nvme_nq < 1)
		nvme_nq = 1;

	/* Allocate the I/O queue pairs (qids 1..nvme_nq). */
	for (i = 0; i < nvme_nq; i++) {
		if (alloc_queue(&io_q[i], i + 1, NVME_QDEPTH) != OK)
			return ENOMEM;
	}

	/* Bring up the I/O queues and learn the namespace geometry.  These admin
	 * commands are still polled (MSI-X is not enabled yet). */
	if ((r = nvme_create_io_queues()) != OK)
		return r;
	if ((r = nvme_identify_namespace()) != OK)
		return r;

	/* All admin work is done: program the single shared MSI-X vector and
	 * switch I/O completion to interrupts. */
	if (nvme_msix_cap != 0) {
		if ((r = nvme_msix_program()) != OK)
			return r;

		nvme_enable_msix(devind);

		if ((r = sys_irqenable(&nvme_msix_hook)) != OK) {
			printf("nvme: cannot enable MSI-X IRQ: %d\n", r);
			return r;
		}
		nvme_use_irq = TRUE;
	}

	/* Re-poll interval for the lost-interrupt backstop timer (~10 ms). */
	nvme_backstop_ticks = sys_hz() / 100;
	if (nvme_backstop_ticks < 1)
		nvme_backstop_ticks = 1;

	printf("nvme%d: %llu blocks of %u bytes (%llu MB), %d I/O queue(s), %s\n",
		nvme_instance, (unsigned long long)ns_blocks, ns_lba_size,
		(unsigned long long)(ns_blocks * ns_lba_size) >> 20,
		nvme_nq, nvme_use_irq ? "MSI-X" : "polled");

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
static int build_prp(struct nvme_queue *q, struct vumap_phys *phys,
	unsigned int nphys, size_t nbytes, u64_t *prp1, u64_t *prp2,
	size_t *covered)
{
	u64_t *prp_list = q->prp_list;
	phys_bytes prp_list_phys = q->prp_list_phys;
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

/* Issue one NVM read/write command on queue `q` with caller-supplied PRPs. */
static int nvme_rw_submit(struct nvme_queue *q, int do_write, u64_t slba,
	u32_t nblocks, u64_t prp1, u64_t prp2)
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

	return nvme_submit_io(q, &cmd);
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
		/* Spread I/O across one worker thread per I/O queue.  Done here
		 * (the first request) because the MT framework resets the worker
		 * count to 1 in its own init, which runs before this callback. */
		blockdriver_mt_set_workers(0, nvme_nq);

		memset(part, 0, sizeof(part));
		memset(subpart, 0, sizeof(subpart));
		part[0].dv_size = ns_blocks * ns_lba_size;
		partition(&nvme_dtab, 0, P_PRIMARY, 0 /* not ATAPI */);
	}

	open_count++;
	return OK;
}

/* Map every minor to the single physical device (id 0) for the MT framework. */
static int nvme_device(devminor_t minor, device_id_t *id)
{
	if (nvme_part(minor) == NULL)
		return ENXIO;

	*id = 0;
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
		struct nvme_queue *q;
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

		/* Claim a queue (and its private PRP-list page) for the rest of
		 * this command, so building the list and the in-flight DMA cannot
		 * race another worker's queue. */
		q = nvme_claim_queue();

		r = build_prp(q, phys, nphys, chunk, &prp1, &prp2, &covered);

		/* build_prp may cover fewer bytes than requested if the buffer is
		 * fragmented into sub-page physical segments (PRP cannot express
		 * those in one command); the loop handles the remainder. */
		if (r == OK) {
			covered -= covered % ns_lba_size;
			if (covered == 0)
				r = EIO;
		}
		if (r == OK) {
			chunk = covered;
			slba = (disk_pos + done) / ns_lba_size;
			nblk = chunk / ns_lba_size;
			r = nvme_rw_submit(q, do_write, slba, nblk, prp1, prp2);
		}

		nvme_release_queue(q);

		if (r != OK)
			return (done > 0) ? (ssize_t)done : r;

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

	blockdriver_mt_task(&nvme_dtab);

	return OK;
}
