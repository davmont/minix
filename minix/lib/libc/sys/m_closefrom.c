#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

/* Close every descriptor from fd up.  It cannot fail, as NetBSD's (and
 * POSIX.1-2024's, which returns nothing) cannot: there being none open is
 * not an error, nor is errno touched.
 */
int closefrom(int fd)
{
	int f, saved_errno = errno;

	if (fd < 0) {
		errno = EBADF;
		return -1;
	}
	for (f = fd; f < OPEN_MAX; f++)
		(void)close(f);
	errno = saved_errno;
	return 0;
}
