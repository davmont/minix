/*
 * POSIX shared memory (shm_open) support for the IPC server.
 *
 * Unlike SysV shm, POSIX shm hands out a real file descriptor so the region
 * can be mmap()ed and passed over a socket with SCM_RIGHTS (this is what
 * Wayland's wl_shm needs).  MINIX has no fd-backed shared memory: mapping a
 * file MAP_SHARED writable copy-on-writes into private pages (see the VM's
 * mem_file.c), so the only way to share writable pages between unrelated
 * processes is vm_remap() - exactly what SysV shmat() uses.
 *
 * We reuse that here.  libc's shm_open() opens a small "token" file (under
 * /var/shm) purely for its name and (dev, ino) identity and its fd, and
 * registers that (dev, ino) with us.  The actual pages are a MAP_ANON region
 * that we hold; libc's mmap() of a shm fd calls us, and we vm_remap() the
 * region into the caller.  Two processes opening the same token file - or one
 * receiving the fd via SCM_RIGHTS - resolve to the same (dev, ino) and hence
 * the same region.  The object lives until shm_unlink() and all mappings are
 * gone, tracked with the same vm_getrefcount() scheme as SysV shm.
 */
#include "inc.h"

#define POSIX_SHM_MAX	128
#define POSIX_SHM_OLD_MAX 64

struct pshm {
	int		used;
	int		unlinked;	/* shm_unlink() seen; free when idle */
	dev_t		dev;
	ino_t		ino;
	vir_bytes	page;		/* held MAP_ANON region, 0 if none yet */
	size_t		size;		/* rounded region size in bytes */
};
static struct pshm pshm_list[POSIX_SHM_MAX];

/*
 * Superseded regions from a grow (see do_shm_map).  A grown object gets a new,
 * larger region, but the old one cannot simply be unmapped: a client that has
 * the object mapped holds a VR_SHARED region whose source is *this* region (see
 * shared_setsource() in the VM's mem_shared.c), so dropping it here would leave
 * that client's mapping dangling on its next page fault.  Park the old region
 * instead and let posix_shm_update() reclaim it once every client has re-mapped
 * and its refcount falls back to ours alone.
 */
struct pshm_old {
	int		used;
	vir_bytes	page;
	size_t		size;
};
static struct pshm_old pshm_old_list[POSIX_SHM_OLD_MAX];

static int
pshm_retire(vir_bytes page, size_t size)
{
	int i;

	for (i = 0; i < POSIX_SHM_OLD_MAX; i++) {
		if (pshm_old_list[i].used)
			continue;
		pshm_old_list[i].used = 1;
		pshm_old_list[i].page = page;
		pshm_old_list[i].size = size;
		return OK;
	}
	return ENOSPC;
}

static struct pshm *
pshm_find(dev_t dev, ino_t ino)
{
	int i;

	for (i = 0; i < POSIX_SHM_MAX; i++)
		if (pshm_list[i].used && !pshm_list[i].unlinked &&
		    pshm_list[i].dev == dev && pshm_list[i].ino == ino)
			return &pshm_list[i];
	return NULL;
}

/* Register a (dev, ino) token as a POSIX shm object (idempotent). */
int
do_shm_open(message *m)
{
	dev_t dev = (dev_t)m->m_lc_ipc_shm.dev;
	ino_t ino = (ino_t)m->m_lc_ipc_shm.ino;
	int i;

	if (pshm_find(dev, ino) != NULL)
		return OK;			/* already registered */

	for (i = 0; i < POSIX_SHM_MAX; i++)
		if (!pshm_list[i].used)
			break;
	if (i == POSIX_SHM_MAX)
		return ENOSPC;

	memset(&pshm_list[i], 0, sizeof(pshm_list[i]));
	pshm_list[i].used = 1;
	pshm_list[i].dev = dev;
	pshm_list[i].ino = ino;
	return OK;
}

/* Map the object for (dev, ino) into the caller via vm_remap. */
int
do_shm_map(message *m)
{
	dev_t dev = (dev_t)m->m_lc_ipc_shm.dev;
	ino_t ino = (ino_t)m->m_lc_ipc_shm.ino;
	size_t size = (size_t)m->m_lc_ipc_shm.size;
	struct pshm *shm;
	void *ret;

	if ((shm = pshm_find(dev, ino)) == NULL)
		return ENOENT;			/* not a shm object; caller falls back */

	if (size == 0)
		return EINVAL;
	size = roundup(size, PAGE_SIZE);

	if (shm->page == 0) {			/* first mapping: allocate pages */
		void *page;

		/*
		 * MAP_USERMEM: these pages are the client's, not ours.  Without it
		 * they are allocated as system memory and draw on the OOM reserve
		 * that exists to keep system services alive -- see VR_USERMEM in
		 * the VM.
		 *
		 * Note there is no memset() here.  MAP_ANON is already zero-filled
		 * on fault (PAF_CLEAR, since the region is not VR_UNINITIALIZED),
		 * so clearing it by hand bought nothing and cost a great deal: it
		 * faulted the whole pool in *our* address space, up front, which is
		 * both eager where the fault should be lazy and, now, the one place
		 * a pool could still evade the reserve.  Leaving the pages untouched
		 * means the client's own fault allocates them, so a failure under
		 * memory pressure sacrifices the client rather than killing us.
		 */
		page = mmap(0, size, PROT_READ | PROT_WRITE,
		    MAP_ANON | MAP_USERMEM, -1, 0);
		if (page == MAP_FAILED)
			return ENOMEM;
		shm->page = (vir_bytes)page;
		shm->size = size;
	} else if (size > shm->size) {
		/*
		 * Grow.  ftruncate() on the token file does not touch our region,
		 * so a pool that has been enlarged only becomes usable when the
		 * next mmap() asks for more than we hold -- which is what
		 * wl_shm_pool_resize() does on every window resize.  Without this
		 * the caller would be handed back a mapping still of the old size
		 * and would fault as soon as it wrote past the end.
		 *
		 * MINIX has no mremap(2) and the VM offers no way to extend a
		 * region in place, so growing means a new region plus a copy.  The
		 * old region is retired rather than freed (see pshm_retire).
		 *
		 * The copy means the old and new regions are distinct memory, where
		 * POSIX would have kept the overlapping pages identical.  A mapper
		 * that never re-mmap()s after a resize therefore keeps writing to
		 * the old region and diverges.  That is safe for the case this
		 * exists to serve: on a wl_shm_pool.resize both sides re-map, and
		 * the wire order (resize precedes any later commit) guarantees the
		 * compositor has re-mapped before it reads the new contents.
		 */
		void *page;

		/* MAP_USERMEM again, and here it is load-bearing: the memcpy below
		 * faults the new pool in *our* address space, before any client has
		 * mapped it, so do_remap()'s tagging would come too late. */
		page = mmap(0, size, PROT_READ | PROT_WRITE,
		    MAP_ANON | MAP_USERMEM, -1, 0);
		if (page == MAP_FAILED)
			return ENOMEM;
		/* The tail beyond the old contents is zero-filled on fault. */
		memcpy(page, (void *)shm->page, shm->size);

		if (pshm_retire(shm->page, shm->size) != OK) {
			munmap(page, size);
			return ENOMEM;
		}

		shm->page = (vir_bytes)page;
		shm->size = size;
	}

	/*
	 * vm_remap() always transfers the whole region, so a caller asking for
	 * less than we hold simply gets more than it asked for -- harmless.  It
	 * must never get less, which is what the grow above guarantees.
	 */
	ret = vm_remap(m->m_source, sef_self(), NULL, (void *)shm->page,
	    shm->size);
	if (ret == MAP_FAILED)
		return ENOMEM;

	m->m_lc_ipc_shm.retaddr = ret;
	return OK;
}

/* Mark the object unlinked; it is freed once no one has it mapped. */
int
do_shm_unlink(message *m)
{
	dev_t dev = (dev_t)m->m_lc_ipc_shm.dev;
	ino_t ino = (ino_t)m->m_lc_ipc_shm.ino;
	struct pshm *shm;

	if ((shm = pshm_find(dev, ino)) == NULL)
		return ENOENT;
	shm->unlinked = 1;
	posix_shm_update();
	return OK;
}

/* Reclaim unlinked objects, and grown-away regions, that no process maps. */
void
posix_shm_update(void)
{
	int i;
	u8_t rc;

	for (i = 0; i < POSIX_SHM_MAX; i++) {
		if (!pshm_list[i].used || pshm_list[i].page == 0)
			continue;
		rc = vm_getrefcount(sef_self(), (void *)pshm_list[i].page);
		if (rc == (u8_t)-1)
			continue;
		/* rc counts our own mapping plus each mapper; idle at 1. */
		if (rc <= 1 && pshm_list[i].unlinked) {
			munmap((void *)pshm_list[i].page, pshm_list[i].size);
			memset(&pshm_list[i], 0, sizeof(pshm_list[i]));
		}
	}

	/*
	 * A region superseded by a grow is dropped as soon as the last client
	 * that still had it mapped has gone (or re-mapped), which is when its
	 * refcount falls back to our own mapping alone.  Doing it here, rather
	 * than at grow time, is what keeps those clients from dangling.
	 */
	for (i = 0; i < POSIX_SHM_OLD_MAX; i++) {
		if (!pshm_old_list[i].used)
			continue;
		rc = vm_getrefcount(sef_self(), (void *)pshm_old_list[i].page);
		if (rc == (u8_t)-1)
			continue;
		if (rc <= 1) {
			munmap((void *)pshm_old_list[i].page,
			    pshm_old_list[i].size);
			memset(&pshm_old_list[i], 0, sizeof(pshm_old_list[i]));
		}
	}
}
