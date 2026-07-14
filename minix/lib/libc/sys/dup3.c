#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

/*
 * dup3() is dup2() that can also set close-on-exec on the new descriptor in one
 * step, and that refuses -- rather than quietly succeeding -- when the two
 * descriptors are the same.  Qt uses it (its process code wants a CLOEXEC dup
 * without a window where the fd could leak across a concurrent exec).
 *
 * MINIX has no dup3 system call, so this is built from dup2() plus fcntl().
 * The composition is not atomic with respect to a concurrent fork+exec, which
 * is the one thing dup3() exists to guarantee; on a single-threaded caller,
 * which is how it is normally used, that distinction does not arise.
 */
int
dup3(int from, int to, int flags)
{
	int rc;

	/* Unlike dup2(), dup3() is required to fail here rather than no-op. */
	if (from == to) {
		errno = EINVAL;
		return -1;
	}

	if ((flags & ~O_CLOEXEC) != 0) {
		errno = EINVAL;
		return -1;
	}

	if ((rc = dup2(from, to)) == -1)
		return -1;

	if ((flags & O_CLOEXEC) != 0) {
		if (fcntl(to, F_SETFD, FD_CLOEXEC) == -1) {
			int saved = errno;

			(void)close(to);
			errno = saved;
			return -1;
		}
	}

	return rc;
}
