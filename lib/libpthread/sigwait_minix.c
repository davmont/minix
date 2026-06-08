/*	$NetBSD$	*/

/*-
 * MINIX userland sigwait(3).
 *
 * MINIX has no synchronous signal-wait primitive (no sigtimedwait(2) syscall),
 * and NetBSD's cancellation-aware sigwait in pthread_cancelstub.c is compiled
 * out on MINIX.  Implement sigwait the way GNU Hurd does -- entirely in userland
 * on top of the existing signal-delivery machinery (sigaction + sigsuspend),
 * rather than adding a kernel primitive (Hurd/Mach keep signals out of the
 * microkernel too).
 *
 * sigwait() atomically unblocks the requested set, suspends until one of those
 * signals is delivered, then restores the prior disposition and mask and reports
 * which signal fired.  The caller is expected (per POSIX) to have those signals
 * blocked beforehand so no handler runs spuriously.
 *
 * Limitation: a single process-global "which signal fired" cell means concurrent
 * sigwait() calls in different threads waiting on overlapping sets are not
 * distinguished; BIND (the motivating consumer) uses one dedicated signal thread,
 * which this serves correctly.
 */

#include <sys/cdefs.h>

#include <errno.h>
#include <signal.h>
#include <stddef.h>

static volatile sig_atomic_t sigwait__caught;

static void
sigwait__catcher(int signo)
{
	sigwait__caught = (sig_atomic_t)signo;
}

int
sigwait(const sigset_t * __restrict set, int * __restrict sig)
{
	struct sigaction sa, osa[NSIG];
	char installed[NSIG];
	sigset_t curmask, suspendmask;
	int s, caught, error = 0;

	if (set == NULL || sig == NULL)
		return EINVAL;

	/* Current (calling-thread) signal mask; we suspend with the set cleared. */
	if (sigprocmask(SIG_SETMASK, NULL, &curmask) != 0)
		return errno;
	suspendmask = curmask;

	sa.sa_handler = sigwait__catcher;
	sa.sa_flags = 0;
	(void)sigemptyset(&sa.sa_mask);

	for (s = 0; s < NSIG; s++)
		installed[s] = 0;

	/* Install our catcher for each requested signal; unblock it for suspend. */
	for (s = 1; s < NSIG; s++) {
		if (sigismember(set, s) == 1) {
			if (sigaction(s, &sa, &osa[s]) == 0)
				installed[s] = 1;
			(void)sigdelset(&suspendmask, s);
		}
	}

	sigwait__caught = 0;

	/*
	 * Atomically install suspendmask and wait.  Returns (with -1/EINTR) once
	 * a now-unblocked signal's handler has run; sigsuspend restores curmask.
	 */
	(void)sigsuspend(&suspendmask);
	caught = (int)sigwait__caught;

	/* Restore the previous dispositions. */
	for (s = 1; s < NSIG; s++) {
		if (installed[s])
			(void)sigaction(s, &osa[s], NULL);
	}

	if (caught == 0)
		error = EINTR;	/* woke for a signal outside the set */
	else
		*sig = caught;

	return error;
}
