#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <string.h>
#include <unistd.h>

int _sys_close(int);

int
_sys_close(int fd)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lc_vfs_close.fd = fd;
	m.m_lc_vfs_close.nblock = 0;

	return _syscall(VFS_PROC_NR, VFS_CLOSE, &m);
}

#ifdef __weak_alias
/* libpthread's cancellation points (pthread_cancelstub.c) override these. */
__weak_alias(close, _sys_close)
__weak_alias(_close, _sys_close)
#endif
