#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <sys/event.h>
#include <fcntl.h>
#include <string.h>

int _sys___kevent50(int, const struct kevent *, size_t, struct kevent *, size_t, const struct timespec *);

/*
 * kqueue(2) and kevent(2): VFS keeps the queues (minix/servers/vfs/kqueue.c).
 */
int
kqueue1(int flags)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lc_vfs_kqueue.flags = flags;

	return _syscall(VFS_PROC_NR, VFS_KQUEUE, &m);
}

int
kqueue(void)
{

	return kqueue1(0);
}

int
_sys___kevent50(int fd, const struct kevent *changelist, size_t nchanges,
	struct kevent *eventlist, size_t nevents,
	const struct timespec *timeout)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lc_vfs_kevent.fd = fd;
	m.m_lc_vfs_kevent.changelist = (vir_bytes)changelist;
	m.m_lc_vfs_kevent.nchanges = (int)nchanges;
	m.m_lc_vfs_kevent.eventlist = (vir_bytes)eventlist;
	m.m_lc_vfs_kevent.nevents = (int)nevents;
	m.m_lc_vfs_kevent.timeout = (vir_bytes)timeout;

	return _syscall(VFS_PROC_NR, VFS_KEVENT, &m);
}

#ifdef __weak_alias
/* libpthread's cancellation points (pthread_cancelstub.c) override these. */
__weak_alias(__kevent50, _sys___kevent50)
#endif
