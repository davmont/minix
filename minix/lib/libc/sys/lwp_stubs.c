#include <sys/cdefs.h>
#include "namespace.h"

#include <lwp.h>
#include <sched.h>
#include <errno.h>

/*
 * Stubs for thread/scheduling primitives that libpthread references but MINIX
 * does not yet implement.  None are on the core mutex/cond/join path.
 */

/*
 * Per-thread (LWP-directed) signal.  MINIX has no per-LWP signal delivery yet,
 * so pthread_kill() fails with ENOSYS.  TODO: a PM-backed _lwp_kill.
 */
int
_lwp_kill(lwpid_t lwp, int sig)
{
	(void)lwp;
	(void)sig;
	errno = ENOSYS;
	return -1;
}

/*
 * Priority-ceiling protection for PTHREAD_PRIO_PROTECT mutexes.  MINIX has no
 * per-thread priority-ceiling call; treat it as a no-op.  Such mutexes still
 * provide mutual exclusion, just without priority elevation.
 */
int
_sched_protect(int priority)
{
	(void)priority;
	return 0;
}
