/*
 * pollts(2) for MINIX.
 *
 * MINIX's VFS has no atomic pollts kernel call, so this wraps the existing
 * poll(2) emulation (itself layered on select): temporarily install the
 * caller's signal mask, run poll() with the timespec converted to a
 * millisecond timeout, then restore the mask.
 *
 * NOTE: as with pselect(2), the mask swap and the poll() are NOT atomic, so a
 * signal delivered in the window between sigprocmask() and poll() can be
 * missed for the duration of the call.  This matches the hand-rolled shim
 * MINIX consumers (e.g. dhcpcd's eloop) already use; it is provided so they
 * can call the standard interface.  Code needing true atomicity must not rely
 * on it.
 */

#include <sys/cdefs.h>
#include "namespace.h"

#include <sys/poll.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>

int
pollts(struct pollfd * __restrict fds, nfds_t nfds,
	const struct timespec * __restrict timeout,
	const sigset_t * __restrict sigmask)
{
	sigset_t omask;
	int r, smask = 0, ms;

	if (timeout == NULL) {
		ms = -1;			/* block indefinitely */
	} else {
		long long msll;

		/* Round up to the next millisecond, clamp to INT_MAX. */
		msll = (long long)timeout->tv_sec * 1000 +
		    (timeout->tv_nsec + 999999) / 1000000;
		ms = (msll > INT_MAX) ? INT_MAX : (int)msll;
	}

	if (sigmask != NULL) {
		if (sigprocmask(SIG_SETMASK, sigmask, &omask) != 0)
			return -1;
		smask = 1;
	}

	r = poll(fds, nfds, ms);

	if (smask) {
		int saved_errno = errno;
		(void)sigprocmask(SIG_SETMASK, &omask, NULL);
		errno = saved_errno;
	}

	return r;
}

#if defined(__minix) && defined(__weak_alias)
__weak_alias(pollts, __pollts50)
#endif
