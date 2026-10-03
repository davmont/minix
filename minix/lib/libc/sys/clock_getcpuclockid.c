#include <sys/cdefs.h>
#include "namespace.h"

#include <sys/types.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

/*
 * clock_getcpuclockid(3): the CPU time clock of process 'pid' is
 * CLOCK_PROCESS_CPUTIME_ID with the pid in its low bits (0: the caller), which
 * PM decodes in clock_gettime(2).  Returns an error number, as POSIX says.
 */
int
clock_getcpuclockid(pid_t pid, clockid_t *clock_id)
{
	int saved_errno = errno;

	if (pid < 0 || pid > 0x1fffffff)
		return ESRCH;
	if (pid != 0 && pid != getpid() && kill(pid, 0) != 0 &&
	    errno == ESRCH) {
		errno = saved_errno;
		return ESRCH;
	}
	errno = saved_errno;
	*clock_id = CLOCK_PROCESS_CPUTIME_ID | (pid == getpid() ? 0 : pid);
	return 0;
}
