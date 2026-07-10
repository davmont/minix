/*	POSIX shared memory - shm_open(2), shm_unlink(3)			*/
/*
 * MINIX has no fd-backed writable shared memory (mapping a file MAP_SHARED
 * writable copy-on-writes into private pages), so POSIX shm is implemented on
 * top of the IPC server's vm_remap mechanism - the same one SysV shm uses.
 *
 * shm_open() opens a small token file under /var/shm; its name gives the
 * object identity and its (dev, ino) key the shared region held by the IPC
 * server.  The returned fd is a real fd (mmap-able, SCM_RIGHTS-passable); its
 * mapping is redirected to the IPC region by __minix_shm_mmap(), called from
 * mmap().  See minix/servers/ipc/posix_shm.c.
 */
#define _SYSTEM	1

#include <sys/cdefs.h>
#include <lib.h>
#include "namespace.h"

#include <minix/rs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifdef __weak_alias
__weak_alias(shm_open, _shm_open)
__weak_alias(shm_unlink, _shm_unlink)
#endif

#define SHM_DIR		"/var/shm/"

static int
ipc_endpt(endpoint_t *pt)
{
	return minix_rs_lookup("ipc", pt);
}

/*
 * Map a POSIX shm name to a token-file path.  POSIX allows only a leading
 * slash and no other slashes; be lenient about leading slashes, strict about
 * embedded ones.
 */
static int
shm_path(const char *name, char *buf, size_t bufsz)
{
	while (*name == '/')
		name++;
	if (*name == '\0' || strchr(name, '/') != NULL) {
		errno = EINVAL;
		return -1;
	}
	if ((size_t)snprintf(buf, bufsz, "%s%s", SHM_DIR, name) >= bufsz) {
		errno = ENAMETOOLONG;
		return -1;
	}
	return 0;
}

int
shm_open(const char *name, int oflag, mode_t mode)
{
	char path[PATH_MAX];
	message m;
	struct stat st;
	endpoint_t pt;
	int fd, err;

	if (shm_path(name, path, sizeof(path)) < 0)
		return -1;

	if ((fd = open(path, oflag, mode)) < 0)
		return -1;
	if (fstat(fd, &st) < 0)
		goto fail;

	if (ipc_endpt(&pt) != OK) {
		errno = ENOSYS;
		goto fail;
	}
	memset(&m, 0, sizeof(m));
	m.m_lc_ipc_shm.dev = st.st_dev;
	m.m_lc_ipc_shm.ino = st.st_ino;
	if (_syscall(pt, IPC_SHM_OPEN, &m) != OK)
		goto fail;

	return fd;

fail:
	err = errno;
	close(fd);
	errno = err;
	return -1;
}

int
shm_unlink(const char *name)
{
	char path[PATH_MAX];
	message m;
	struct stat st;
	endpoint_t pt;

	if (shm_path(name, path, sizeof(path)) < 0)
		return -1;
	if (stat(path, &st) < 0)
		return -1;
	if (ipc_endpt(&pt) == OK) {
		memset(&m, 0, sizeof(m));
		m.m_lc_ipc_shm.dev = st.st_dev;
		m.m_lc_ipc_shm.ino = st.st_ino;
		(void)_syscall(pt, IPC_SHM_UNLINK, &m);
	}
	return unlink(path);
}
