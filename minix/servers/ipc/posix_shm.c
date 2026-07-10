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

struct pshm {
	int		used;
	int		unlinked;	/* shm_unlink() seen; free when idle */
	dev_t		dev;
	ino_t		ino;
	vir_bytes	page;		/* held MAP_ANON region, 0 if none yet */
	size_t		size;		/* rounded region size in bytes */
};
static struct pshm pshm_list[POSIX_SHM_MAX];

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

	if (shm->page == 0) {			/* first mapping: allocate pages */
		void *page;

		if (size == 0)
			return EINVAL;
		size = roundup(size, PAGE_SIZE);
		page = mmap(0, size, PROT_READ | PROT_WRITE, MAP_ANON, -1, 0);
		if (page == MAP_FAILED)
			return ENOMEM;
		memset(page, 0, size);
		shm->page = (vir_bytes)page;
		shm->size = size;
	}

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

/* Reclaim unlinked objects that no process maps anymore. */
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
}
