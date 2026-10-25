#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <sys/types.h>
#include <errno.h>
#include <sched.h>
#include <string.h>
#include <time.h>
#include <minix/config.h>

/*
 * POSIX process scheduling (sched_*).  SCHED_OTHER is time-sharing, its
 * priority moved by nice(3) and by CPU use, and has the single priority 0.
 * SCHED_FIFO and SCHED_RR have the priorities RT_PRIO_MIN..RT_PRIO_MAX, above
 * every time-sharing process and below the system services; setting them
 * takes root.  PM keeps the policy of each process, and of each thread, which
 * has a slot of its own (pthread_setschedparam(3)).  sched_setscheduler() and
 * sched_setparam() on a process set that of its first thread; 0 is the
 * calling thread.
 */

#undef sched_yield		/* <sched.h> renames it to __libc_thr_yield */

int _sys_sched_yield(void);

static int
sched_call(int call, pid_t pid, lwpid_t lid, int policy, int prio,
	message *m)
{

	if (pid < 0) {
		errno = EINVAL;
		return -1;
	}
	memset(m, 0, sizeof(*m));
	m->m_lc_pm_sched.pid = pid;
	m->m_lc_pm_sched.lid = lid;
	m->m_lc_pm_sched.policy = policy;
	m->m_lc_pm_sched.prio = prio;
	return _syscall(PM_PROC_NR, call, m);
}

static int
set_param(pid_t pid, lwpid_t lid, int policy, const struct sched_param *param)
{
	message m;

	if (param == NULL) {
		errno = EINVAL;
		return -1;
	}
	return sched_call(PM_SCHED_SETPARAM, pid, lid, policy,
	    param->sched_priority, &m);
}

static int
get_param(pid_t pid, lwpid_t lid, int *policy, struct sched_param *param)
{
	message m;

	if (sched_call(PM_SCHED_GETPARAM, pid, lid, 0, 0, &m) < 0)
		return -1;
	if (policy != NULL)
		*policy = m.m_lc_pm_sched.policy;
	if (param != NULL) {
		memset(param, 0, sizeof(*param));
		param->sched_priority = m.m_lc_pm_sched.prio;
	}
	return 0;
}

int
sched_get_priority_min(int policy)
{

	switch (policy) {
	case SCHED_OTHER:
		return 0;
	case SCHED_FIFO:
	case SCHED_RR:
		return RT_PRIO_MIN;
	default:
		errno = EINVAL;
		return -1;
	}
}

int
sched_get_priority_max(int policy)
{

	switch (policy) {
	case SCHED_OTHER:
		return 0;
	case SCHED_FIFO:
	case SCHED_RR:
		return RT_PRIO_MAX;
	default:
		errno = EINVAL;
		return -1;
	}
}

int
sched_getscheduler(pid_t pid)
{
	int policy;

	if (get_param(pid, 0, &policy, NULL) != 0)
		return -1;
	return policy;
}

int
sched_getparam(pid_t pid, struct sched_param *param)
{

	if (param == NULL) {
		errno = EINVAL;
		return -1;
	}
	return get_param(pid, 0, NULL, param);
}

int
sched_setparam(pid_t pid, const struct sched_param *param)
{

	if (set_param(pid, 0, SCHED_NONE, param) < 0)
		return -1;
	return 0;
}

/* Returns the previous policy. */
int
sched_setscheduler(pid_t pid, int policy, const struct sched_param *param)
{

	if (policy == SCHED_NONE) {	/* not one to set */
		errno = EINVAL;
		return -1;
	}
	return set_param(pid, 0, policy, param);
}

int
__sched_rr_get_interval50(pid_t pid, struct timespec *interval)
{
	int policy, ms;

	if (interval == NULL) {
		errno = EINVAL;
		return -1;
	}
	if (get_param(pid, 0, &policy, NULL) != 0)
		return -1;
	/* The quantum the scheduler gives it: SCHED_FIFO has none. */
	switch (policy) {
	case SCHED_RR:		ms = RR_QUANTUM;	break;
	case SCHED_FIFO:	ms = 0;			break;
	default:		ms = USER_QUANTUM;	break;
	}
	interval->tv_sec = ms / 1000;
	interval->tv_nsec = (ms % 1000) * 1000000L;
	return 0;
}

/*
 * The system call: PM has the scheduler treat it as the end of the caller's
 * quantum, so the caller drops behind other runnable work (see do_yield in
 * the SCHED server).
 */
int
_sys_sched_yield(void)
{
	message m;

	memset(&m, 0, sizeof(m));
	return _syscall(PM_PROC_NR, PM_SCHED_YIELD, &m);
}

/* The public name, for callers that did not include <sched.h>. */
int
sched_yield(void)
{

	return _sys_sched_yield();
}

/*
 * The per-thread variants libpthread uses (pthread_[gs]etschedparam): 'lid'
 * is the thread's (_lwp_self()).  SCHED_NONE keeps the policy.
 */
int
_sched_setparam(pid_t pid, lwpid_t lid, int policy,
	const struct sched_param *param)
{

	if (set_param(pid, lid, policy, param) < 0)
		return -1;
	return 0;
}

int
_sched_getparam(pid_t pid, lwpid_t lid, int *policy, struct sched_param *param)
{

	return get_param(pid, lid, policy, param);
}

/*
 * CPU affinity: PM keeps, for each thread, a mask of the CPUs it may run on
 * (0: any), which SCHED keeps it to.  A cpuset_t of 'size' bytes is turned
 * into the mask and back.  lid 0 (P_ALL_LWPS) sets it for every thread of
 * the process, and gets that of the calling thread (its own process) or of
 * the first one.  An empty set is any CPU, as on NetBSD.
 */
#define MASK_CPUS	32		/* the CPUs a mask can name */
#ifndef P_ALL_LWPS
#define P_ALL_LWPS	0		/* every thread, as on NetBSD */
#endif

int
_sched_setaffinity(pid_t pid, lwpid_t lid, size_t size, const cpuset_t *set)
{
	message m;
	uint32_t mask = 0;
	cpuid_t i;
	int r;

	if (pid < 0 || set == NULL) {
		errno = EINVAL;
		return -1;
	}
	for (i = 0; i < size * NBBY; i++) {
		if ((r = cpuset_isset(i, set)) < 0)
			break;			/* past the end of the set */
		if (r == 0)
			continue;
		if (i >= MASK_CPUS) {
			errno = EINVAL;
			return -1;
		}
		mask |= 1U << i;
	}
	memset(&m, 0, sizeof(m));
	m.m_lc_pm_affinity.pid = pid;
	m.m_lc_pm_affinity.lid = lid;
	m.m_lc_pm_affinity.set = 1;
	m.m_lc_pm_affinity.cpumask = mask;
	return _syscall(PM_PROC_NR, PM_SCHED_AFFINITY, &m);
}

int
_sched_getaffinity(pid_t pid, lwpid_t lid, size_t size, cpuset_t *set)
{
	message m;
	cpuid_t i;

	if (pid < 0 || set == NULL) {
		errno = EINVAL;
		return -1;
	}
	memset(&m, 0, sizeof(m));
	m.m_lc_pm_affinity.pid = pid;
	m.m_lc_pm_affinity.lid = lid;
	if (_syscall(PM_PROC_NR, PM_SCHED_AFFINITY, &m) < 0)
		return -1;
	memset(set, 0, size);
	for (i = 0; i < MASK_CPUS; i++) {
		if (!(m.m_lc_pm_affinity.cpumask & (1U << i)))
			continue;
		if (i >= size * NBBY || cpuset_set(i, set) < 0) {
			errno = EINVAL;		/* the set is too small */
			return -1;
		}
	}
	return 0;
}

int
sched_getaffinity_np(pid_t pid, size_t size, cpuset_t *set)
{

	return _sched_getaffinity(pid, P_ALL_LWPS, size, set);
}

int
sched_setaffinity_np(pid_t pid, size_t size, cpuset_t *set)
{

	return _sched_setaffinity(pid, P_ALL_LWPS, size, set);
}
