#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <sys/mman.h>
#include <errno.h>

/*
 * madvise(2) and posix_madvise(3) are advisory: they tell the kernel how memory
 * is expected to be used, and an implementation is free to do nothing with that
 * information.  POSIX says so of posix_madvise in as many words -- it "shall
 * have no effect on the semantics of the application".
 *
 * MINIX's VM does not act on the advice, so these do nothing and succeed.  That
 * is a conforming implementation, and it is what the headers have been
 * promising all along: both were declared in <sys/mman.h> but neither existed
 * in the library, so any caller failed at link time with an undefined reference
 * to a function the header said was there.  pcre2's JIT (and so Qt) is one such
 * caller.
 */

int
madvise(void *addr, size_t len, int behav)
{
	(void)addr;
	(void)len;

	switch (behav) {
	case MADV_NORMAL:
	case MADV_RANDOM:
	case MADV_SEQUENTIAL:
	case MADV_WILLNEED:
	case MADV_DONTNEED:
	case MADV_FREE:
		return 0;
	default:
		errno = EINVAL;
		return -1;
	}
}

int
posix_madvise(void *addr, size_t len, int advice)
{
	/*
	 * posix_madvise() differs from madvise() in its error reporting: it
	 * returns the error number rather than setting errno.
	 */
	if (madvise(addr, len, advice) != 0)
		return errno;
	return 0;
}
