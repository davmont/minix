#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <lwp.h>
#include <time.h>
#include <string.h>
#include <stdint.h>

#ifdef __weak_alias
__weak_alias(_lwp_park, ___lwp_park60)
#endif

/*
 * Block the calling thread until another thread unparks it (by lwpid).  The
 * 'hint'/'unparkhint' addresses are advisory lookup hints in the NetBSD kernel
 * implementation; PM addresses threads by lwpid directly, so they are unused.
 * The 'unpark' argument optionally unparks another thread first.
 *
 * NB: timeouts are not yet honoured by PM (a timed park blocks like an untimed
 * one); the timeout is still passed through so the ABI is forward-compatible.
 */
int
___lwp_park60(clockid_t clock_id, int flags, const struct timespec *ts,
    lwpid_t unpark, const void *hint, const void *unparkhint)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lc_pm_lwp_park.unpark = (int32_t) unpark;
	m.m_lc_pm_lwp_park.flags = (uint32_t) flags;
	m.m_lc_pm_lwp_park.clock_id = (int32_t) clock_id;
	if (ts != NULL) {
		m.m_lc_pm_lwp_park.has_timeout = 1;
		m.m_lc_pm_lwp_park.sec = (int64_t) ts->tv_sec;
		m.m_lc_pm_lwp_park.nsec = (int32_t) ts->tv_nsec;
	}
	(void) hint;
	(void) unparkhint;

	return _syscall(PM_PROC_NR, PM_LWP_PARK, &m);
}
