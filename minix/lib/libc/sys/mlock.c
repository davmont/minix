#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <sys/mman.h>
#include <errno.h>
#include <unistd.h>

/*
 * mlock(2), munlock(2), mlockall(2) and munlockall(2).  <unistd.h> has said
 * _POSIX_MEMLOCK and _POSIX_MEMLOCK_RANGE all along, and <sys/mman.h>
 * declared the calls, but the library did not have them: a program that
 * used them failed to link.
 *
 * They check what POSIX has them check, and succeed, but VM does not pin
 * the pages: a locked page may still be compressed or swapped out when
 * memory runs short, as any other.  Only the superuser may lock, as on
 * NetBSD with no RLIMIT_MEMLOCK to spare.  The range must be mapped
 * (ENOMEM) and start on a page (EINVAL); VM's msync(2) checks the very same,
 * and MS_ASYNC has it do nothing else.
 */

static int
check_range(const void *addr, size_t len)
{

	return msync(__UNCONST(addr), len, MS_ASYNC);
}

int
mlock(const void *addr, size_t len)
{

	if (geteuid() != 0) {
		errno = EPERM;
		return -1;
	}
	return check_range(addr, len);
}

int
munlock(const void *addr, size_t len)
{

	return check_range(addr, len);
}

int
mlockall(int flags)
{

	if (flags == 0 || (flags & ~(MCL_CURRENT | MCL_FUTURE)) != 0) {
		errno = EINVAL;
		return -1;
	}
	if (geteuid() != 0) {
		errno = EPERM;
		return -1;
	}
	return 0;
}

int
munlockall(void)
{

	return 0;
}
