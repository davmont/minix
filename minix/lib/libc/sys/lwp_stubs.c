#include <sys/cdefs.h>
#include <lib.h>
#include "namespace.h"

#include <string.h>
#include <lwp.h>
#include <sched.h>
#include <errno.h>

/*
 * Stubs for thread/scheduling primitives that libpthread references but MINIX
 * does not yet implement.  None are on the core mutex/cond/join path.
 */

/*
 * Per-thread (LWP-directed) signal, the core of pthread_kill(3): PM delivers
 * it to that thread only, if it belongs to our process.
 */
int
_lwp_kill(lwpid_t lwp, int sig)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lc_pm_lwp_kill.target = lwp;
	m.m_lc_pm_lwp_kill.sig = sig;
	return _syscall(PM_PROC_NR, PM_LWP_KILL, &m);
}

/*
 * Priority-ceiling protection for PTHREAD_PRIO_PROTECT mutexes: libpthread
 * calls this with the ceiling of a mutex the caller locks, and with -1 when
 * it unlocks one.  PM runs the thread at the highest ceiling it holds (only
 * if it is root: real-time priority is root's; anyone else keeps its own).
 */
int
_sched_protect(int priority)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lc_pm_sched.prio = priority;
	return _syscall(PM_PROC_NR, PM_SCHED_PROTECT, &m);
}

/*
 * Priority inheritance for PTHREAD_PRIO_INHERIT mutexes, whose owner word is
 * at 'key'.  libpthread calls this with SCHED_INHERIT_WAIT before the caller
 * parks on one held by thread 'lid', having seen 'owner' in the word, and
 * with SCHED_INHERIT_DONE once it is awake; the thread that unlocks one calls
 * it with SCHED_INHERIT_RELEASE.  PM runs the owner at the highest priority
 * of the threads waiting for it, as long as they do.  A WAIT for a mutex that
 * PM no longer finds as 'owner' is EAGAIN: it was let go meanwhile.
 */
int
_sched_inherit(int op, lwpid_t lid, volatile void *key, void *owner)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lc_pm_inherit.op = op;
	m.m_lc_pm_inherit.lid = lid;
	m.m_lc_pm_inherit.key = (vir_bytes)key;
	m.m_lc_pm_inherit.owner = (vir_bytes)owner;
	return _syscall(PM_PROC_NR, PM_SCHED_INHERIT, &m);
}
