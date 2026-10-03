#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <sys/types.h>
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <minix/config.h>

/*
 * POSIX process scheduling (sched_*).  The MINIX scheduler gives user
 * processes a time-sharing policy only: priorities move with nice(3) and with
 * CPU use, which is SCHED_OTHER.  SCHED_FIFO and SCHED_RR are not offered,
 * so they are refused with EINVAL, and SCHED_OTHER has the single priority 0.
 */

#undef sched_yield		/* <sched.h> renames it to __libc_thr_yield */

int _sys_sched_yield(void);

/* Check that 'pid' names a process (0 is the caller).  To change its
 * parameters, the caller must also be allowed to signal it.
 */
static int
check_pid(pid_t pid, int set)
{

	if (pid < 0) {
		errno = EINVAL;
		return -1;
	}
	if (pid == 0 || kill(pid, 0) == 0)
		return 0;
	if (errno == EPERM && !set)
		return 0;
	return -1;			/* ESRCH, or EPERM to change it */
}

static int
check_param(int policy, const struct sched_param *param)
{

	if (policy != SCHED_OTHER || param == NULL ||
	    param->sched_priority != 0) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}

int
sched_get_priority_min(int policy)
{

	if (policy != SCHED_OTHER) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}

int
sched_get_priority_max(int policy)
{

	return sched_get_priority_min(policy);
}

int
sched_getscheduler(pid_t pid)
{

	if (check_pid(pid, 0) != 0)
		return -1;
	return SCHED_OTHER;
}

int
sched_getparam(pid_t pid, struct sched_param *param)
{

	if (param == NULL) {
		errno = EINVAL;
		return -1;
	}
	if (check_pid(pid, 0) != 0)
		return -1;
	memset(param, 0, sizeof(*param));
	return 0;
}

int
sched_setparam(pid_t pid, const struct sched_param *param)
{

	if (check_param(SCHED_OTHER, param) != 0 || check_pid(pid, 1) != 0)
		return -1;
	return 0;
}

/* Returns the previous policy. */
int
sched_setscheduler(pid_t pid, int policy, const struct sched_param *param)
{

	if (check_param(policy, param) != 0 || check_pid(pid, 1) != 0)
		return -1;
	return SCHED_OTHER;
}

int
__sched_rr_get_interval50(pid_t pid, struct timespec *interval)
{

	if (interval == NULL) {
		errno = EINVAL;
		return -1;
	}
	if (check_pid(pid, 0) != 0)
		return -1;
	/* The quantum a user process gets from the scheduler. */
	interval->tv_sec = USER_QUANTUM / 1000;
	interval->tv_nsec = (USER_QUANTUM % 1000) * 1000000L;
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
 * The per-thread variants libpthread uses (pthread_[gs]etschedparam).  As
 * for processes, SCHED_OTHER at priority 0; SCHED_NONE keeps the policy.
 */
int
_sched_setparam(pid_t pid, lwpid_t lid, int policy,
	const struct sched_param *param)
{

	(void)lid;
	if (policy == SCHED_NONE)
		policy = SCHED_OTHER;
	if (check_param(policy, param) != 0 || check_pid(pid, 1) != 0)
		return -1;
	return 0;
}

int
_sched_getparam(pid_t pid, lwpid_t lid, int *policy, struct sched_param *param)
{

	(void)lid;
	if (check_pid(pid, 0) != 0)
		return -1;
	if (policy != NULL)
		*policy = SCHED_OTHER;
	if (param != NULL)
		memset(param, 0, sizeof(*param));
	return 0;
}
