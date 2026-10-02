#include <sys/cdefs.h>
#include "namespace.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

/*
 * Fill 'buf' with 'len' (at most GETENTROPY_MAX) bytes of entropy, from the
 * random driver, as arc4random(3) does here (MINIX has no KERN_ARND).
 */
int
getentropy(void *buf, size_t len)
{
	char *p = buf;
	ssize_t r;
	int fd, err;

	if (len > GETENTROPY_MAX) {
		errno = EINVAL;
		return -1;
	}

	if ((fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC)) < 0) {
		errno = EIO;
		return -1;
	}

	while (len > 0) {
		if ((r = read(fd, p, len)) <= 0) {
			if (r < 0 && errno == EINTR)
				continue;
			err = (r < 0) ? errno : EIO;
			(void)close(fd);
			errno = (err == EFAULT) ? EFAULT : EIO;
			return -1;
		}
		p += r;
		len -= r;
	}

	(void)close(fd);
	return 0;
}
