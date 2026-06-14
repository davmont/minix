/*
 * pselect(2) for MINIX.
 *
 * MINIX's VFS has no atomic pselect kernel call, so this wraps the existing
 * select(2) emulation: temporarily install the caller's signal mask, run
 * select() with the timespec converted to a timeval, then restore the mask.
 *
 * NOTE: the mask swap and the select() are NOT atomic, so a signal that
 * arrives in the (tiny) window between sigprocmask() and select() can be
 * missed for the duration of the call -- the classic pre-pselect race.  This
 * matches what MINIX consumers (e.g. dhcpcd's eloop) already do by hand; it is
 * provided here so they can use the standard interface instead of open-coding
 * the shim.  Code needing true atomicity must not rely on it.
 */

#include <sys/cdefs.h>
#include "namespace.h"

#include <sys/select.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>

int
pselect(int nfds, fd_set * __restrict readfds, fd_set * __restrict writefds,
	fd_set * __restrict exceptfds,
	const struct timespec * __restrict timeout,
	const sigset_t * __restrict sigmask)
{
	struct timeval tv, *tvp = NULL;
	sigset_t omask;
	int r, smask = 0;

	if (timeout != NULL) {
		TIMESPEC_TO_TIMEVAL(&tv, timeout);
		tvp = &tv;
	}

	if (sigmask != NULL) {
		if (sigprocmask(SIG_SETMASK, sigmask, &omask) != 0)
			return -1;
		smask = 1;
	}

	r = select(nfds, readfds, writefds, exceptfds, tvp);

	if (smask) {
		int saved_errno = errno;
		(void)sigprocmask(SIG_SETMASK, &omask, NULL);
		errno = saved_errno;
	}

	return r;
}

#if defined(__minix) && defined(__weak_alias)
__weak_alias(pselect, __pselect50)
#endif
