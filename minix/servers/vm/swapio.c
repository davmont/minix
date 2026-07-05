/* swapio - asynchronous block I/O from VM to the swap device
 * (RECLAIM_DESIGN.md, phase C).
 *
 * VM writes/reads 4 KB swap slots to a dedicated raw swap partition
 * itself (the data plane), so cold compressed blobs can be paged out to
 * disk and back.  The driver ENDPOINT is supplied by VFS at swapon time
 * (the control plane, do_swapon()); VM never guesses or trusts a
 * user-supplied endpoint.  The I/O is ASYNCHRONOUS: a BDEV_READ/WRITE is
 * asynsend()ed to the driver and the BDEV_REPLY is handled later in VM's
 * main loop (do_swap_reply()), so the single-threaded VM event loop
 * never blocks on the disk.
 *
 * Robustness (this is the memory-reclaim path): everything the hot path
 * needs is reserved at swapon time and NOT allocated under pressure -- a
 * permanently-mapped bounce page and a standing grant on it.  A single
 * I/O is in flight at a time (one bounce page); swapio_busy() lets
 * callers serialize.  Inert until swapon configures a device.
 */

#include <assert.h>
#include <string.h>
#include <minix/com.h>
#include <minix/ipc.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>
#include <minix/safecopies.h>
#include <sys/mman.h>

#include "proto.h"
#include "vm.h"
#include "glo.h"

/* Configured swap driver (NONE => not configured => inert). */
static endpoint_t swap_endpt = NONE;
static int swap_minor;

/* Bounce page the driver DMAs to/from, and a STANDING grant on it (both
 * set up once at swapon, reused for every I/O -> no per-op allocation on
 * the reclaim path). */
static unsigned char *swap_bounce;
static phys_bytes swap_bounce_phys;
static cp_grant_id_t swap_bounce_grant = GRANT_INVALID;

/* The single in-flight request. */
static struct {
	int		 busy;
	int		 id;		/* BDEV correlation id */
	int		 is_read;
	phys_bytes	 dst_phys;	/* read: copy bounce here on reply */
	swapio_cb_t	 cb;
	void		*arg;
} swap_req;

static int swap_next_id = 1;

int swapio_busy(void)		{ return swap_req.busy; }
int swapio_configured(void)	{ return swap_endpt != NONE; }

/*
 * Configure the swap device from the (VFS-resolved) driver endpoint,
 * minor, and slot count.  VFS has already opened the device; here we only
 * reserve the bounce page + standing grant (no driver call -> non-blocking).
 */
int
swapio_configure_ep(endpoint_t driver, int minor, unsigned long nslots)
{

	if (swap_endpt != NONE)
		return EBUSY;			/* already configured */
	if (driver == NONE || nslots == 0)
		return EINVAL;

	if (swap_bounce == NULL &&
	    !(swap_bounce = vm_allocpage(&swap_bounce_phys, VMP_SLAB))) {
		printf("VM: swapio: no bounce page\n");
		return ENOMEM;
	}

	/* The device is already open: VFS (the control plane) opened it in
	 * do_swapctl before handing us the resolved endpoint.  VM never makes
	 * a synchronous driver call -- the pager must not block on the disk.
	 *
	 * Standing grant on the bounce page (both directions). */
	swap_bounce_grant = cpf_grant_direct(driver,
	    (vir_bytes)swap_bounce, VM_PAGE_SIZE, CPF_READ | CPF_WRITE);
	if (!GRANT_VALID(swap_bounce_grant)) {
		printf("VM: swapio: grant setup failed\n");
		return EINVAL;
	}

	swap_endpt = driver;
	swap_minor = minor;
	swapstore_configure(nslots);
	printf("VM: swapio: swap on driver %d minor %d, %lu slots\n",
		driver, minor, nslots);
	return OK;
}

static int
swapio_start(int is_read, unsigned long slot, phys_bytes buf_phys,
	swapio_cb_t cb, void *arg)
{
	message m;
	int r;

	if (swap_endpt == NONE)
		return ENODEV;
	if (swap_req.busy)
		return EBUSY;

	/* write: stage the page into the bounce buffer. */
	if (!is_read) {
		if ((r = sys_abscopy(buf_phys, swap_bounce_phys,
		    VM_PAGE_SIZE)) != OK)
			return r;
	}

	swap_req.busy = 1;
	swap_req.id = swap_next_id++;
	swap_req.is_read = is_read;
	swap_req.dst_phys = buf_phys;
	swap_req.cb = cb;
	swap_req.arg = arg;

	memset(&m, 0, sizeof(m));
	m.m_type = is_read ? BDEV_READ : BDEV_WRITE;
	m.m_lbdev_lblockdriver_msg.minor = swap_minor;
	m.m_lbdev_lblockdriver_msg.pos = (off_t)slot * VM_PAGE_SIZE;
	m.m_lbdev_lblockdriver_msg.count = VM_PAGE_SIZE;
	m.m_lbdev_lblockdriver_msg.grant = swap_bounce_grant;
	m.m_lbdev_lblockdriver_msg.id = swap_req.id;

	if ((r = asynsend3(swap_endpt, &m, 0)) != OK) {
		swap_req.busy = 0;
		return r;
	}
	return OK;
}

/* Write the 4 KB page at buf_phys to swap 'slot'; cb(status,arg) later. */
int
swapio_write_page(unsigned long slot, phys_bytes buf_phys, swapio_cb_t cb,
	void *arg)
{
	return swapio_start(0, slot, buf_phys, cb, arg);
}

/* Read swap 'slot' into the 4 KB frame dst_phys; cb(status,arg) later. */
int
swapio_read_page(unsigned long slot, phys_bytes dst_phys, swapio_cb_t cb,
	void *arg)
{
	return swapio_start(1, slot, dst_phys, cb, arg);
}

/*
 * Handle a BDEV_REPLY from the swap driver (dispatched from main.c).
 * The standing grant persists (not revoked): it covers only the bounce
 * page and is reused for the next I/O.
 */
void
do_swap_reply(message *m)
{
	int status;
	swapio_cb_t cb;
	void *arg;

	if (!swap_req.busy || m->m_source != swap_endpt ||
	    m->m_lblockdriver_lbdev_reply.id != swap_req.id) {
		printf("VM: swapio: stray/mismatched BDEV_REPLY\n");
		return;
	}

	status = m->m_lblockdriver_lbdev_reply.status;

	/* read: deliver from the bounce buffer to its frame. */
	if (swap_req.is_read && status == (int)VM_PAGE_SIZE) {
		if (sys_abscopy(swap_bounce_phys, swap_req.dst_phys,
		    VM_PAGE_SIZE) != OK)
			status = EIO;
	}

	cb = swap_req.cb;
	arg = swap_req.arg;
	swap_req.busy = 0;

	if (cb)
		cb(status, arg);
}

/*===========================================================================*
 *		C1 async round-trip self-test (temporary)		     *
 *===========================================================================*/
/* Exercises the real async data plane once, right after swapon: write a
 * known page to slot 0, read it back, compare.  Result in /proc/meminfo
 * (vsi_swaptest).  Removed once C2/C3 exercise the path for real.  Codes:
 * 0 not-run, 1 OK, 2 in-progress, 12 write-fail, 14 read-fail,
 * 15 mismatch. */
static int swap_st_result;		/* 0 until started */
static unsigned char *swap_st_page;
static phys_bytes swap_st_page_phys;

int swapio_selftest_result(void)	{ return swap_st_result; }

static void
st_read_done(int status, void *arg)
{
	int i;
	(void)arg;
	if (status != (int)VM_PAGE_SIZE) { swap_st_result = 14; return; }
	for (i = 0; i < (int)VM_PAGE_SIZE; i++)
		if (swap_st_page[i] != (unsigned char)(i * 7 + 3)) {
			swap_st_result = 15;
			return;
		}
	swap_st_result = 1;			/* round-trip OK */
}

static void
st_write_done(int status, void *arg)
{
	(void)arg;
	if (status != (int)VM_PAGE_SIZE) { swap_st_result = 12; return; }
	memset(swap_st_page, 0, VM_PAGE_SIZE);	/* scribble before read-back */
	if (swapio_read_page(0, swap_st_page_phys, st_read_done, NULL) != OK)
		swap_st_result = 14;
}

void
swapio_selftest_start(void)
{
	int i;

	if (swap_endpt == NONE || swap_st_result != 0)
		return;
	if (swap_st_page == NULL &&
	    !(swap_st_page = vm_allocpage(&swap_st_page_phys, VMP_SLAB)))
		return;

	for (i = 0; i < (int)VM_PAGE_SIZE; i++)
		swap_st_page[i] = (unsigned char)(i * 7 + 3);
	swap_st_result = 2;			/* in progress */
	if (swapio_write_page(0, swap_st_page_phys, st_write_done, NULL) != OK)
		swap_st_result = 12;
}

/*
 * VM's side of swapon (control plane): VFS has resolved the swap device
 * to a driver endpoint + minor and sends us VM_SWAPON.  Configure the
 * data plane and kick off the one-time round-trip self-test.
 */
int
do_swapon(message *m)
{
	endpoint_t drv = (endpoint_t)m->VMSW_ENDPT;
	int minor = m->VMSW_MINOR;
	unsigned long nslots = (unsigned long)(unsigned)m->VMSW_NSLOTS;
	int r;

	r = swapio_configure_ep(drv, minor, nslots);
	if (r == OK)
		swapio_selftest_start();
	return r;
}
