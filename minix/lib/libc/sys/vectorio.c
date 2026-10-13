#include <sys/cdefs.h>
#include <lib.h>
#include "namespace.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/uio.h>
#include <unistd.h>

/*
 * Return how many bytes of a vector of TOTALLEN bytes are worth passing to a
 * write to FD, with the given send(2) FLAGS: a nonblocking write to a stream
 * socket or a pipe takes no more than fits in its buffer, so copying the rest
 * of a large vector into the temporary buffer is wasted.  This matters: libuv
 * writes its whole queue with writev(2), and when that was 10 MB, every call
 * copied all of it to send 64 KB.  The limit stays well above the buffer size,
 * so that the write behaves exactly as for the whole vector (a pipe write of
 * PIPE_BUF bytes or less, for instance, would be atomic instead).  Checking
 * costs system calls, so small vectors are not checked.
 */
ssize_t
_vectorio_wlimit(int fd, ssize_t totallen, int flags)
{
	struct stat st;
	socklen_t len;
	ssize_t limit;
	int type, size, errno_saved;

	if (totallen <= 2 * PIPE_BUF)
		return totallen;

	errno_saved = errno;
	limit = totallen;
	if (!(flags & MSG_DONTWAIT) &&
	    ((flags = fcntl(fd, F_GETFL)) == -1 || !(flags & O_NONBLOCK)))
		goto out;

	len = sizeof(type);
	if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len) == 0) {
		/* Datagrams must be sent whole, or not at all. */
		len = sizeof(size);
		if (type == SOCK_STREAM && getsockopt(fd, SOL_SOCKET,
		    SO_SNDBUF, &size, &len) == 0 && size > 0 &&
		    (ssize_t)size < totallen / 2)
			limit = 2 * (ssize_t)size;
	} else if (fstat(fd, &st) == 0 && S_ISFIFO(st.st_mode))
		limit = 2 * PIPE_BUF;
out:
	errno = errno_saved;
	return limit;
}

/*
 * Create a single temporary buffer for the entire vector, or for the first
 * LIMIT bytes of it if that is less.  For writes, also copy the actual data
 * into the temporary buffer.
 */
ssize_t
_vectorio_setup(const struct iovec * iov, int iovcnt, char ** ptr, int op,
	int fd, int flags)
{
	char *buffer;
	ssize_t totallen, copied, len;
	int i;

	/* Parameter sanity checks. */
	if (iovcnt < 0 || iovcnt > IOV_MAX) {
		errno = EINVAL;
		return -1;
	}

	totallen = 0;
	for (i = 0; i < iovcnt; i++) {
		/* Do not read/write anything in case of possible overflow. */
		if ((size_t)SSIZE_MAX - totallen < iov[i].iov_len) {
			errno = EINVAL;
			return -1;
		}
		totallen += iov[i].iov_len;

		/* Report on NULL pointers. */
		if (iov[i].iov_len > 0 && iov[i].iov_base == NULL) {
			errno = EFAULT;
			return -1;
		}
	}

	if (op == _VECTORIO_WRITE && fd >= 0)
		totallen = _vectorio_wlimit(fd, totallen, flags);

	/* Anything to do? */
	if (totallen == 0) {
		*ptr = NULL;
		return 0;
	}

	/* Allocate a temporary buffer. */
	buffer = (char *)malloc(totallen);
	if (buffer == NULL)
		return -1;

	/* For writes, copy over the buffer contents before the call. */
	if (op == _VECTORIO_WRITE) {
		copied = 0;
		for (i = 0; i < iovcnt && copied < totallen; i++) {
			len = iov[i].iov_len;
			if (len > totallen - copied)
				len = totallen - copied;
			memcpy(buffer + copied, iov[i].iov_base, len);
			copied += len;
		}
		assert(copied == totallen);
	}

	/* Return the temporary buffer and its size. */
	*ptr = buffer;
	return totallen;
}

/*
 * Clean up the temporary buffer created for the vector.  For successful reads,
 * also copy out the retrieved buffer contents.
 */
void
_vectorio_cleanup(const struct iovec * iov, int iovcnt, char * buffer,
	ssize_t r, int op)
{
	int i, errno_saved;
	ssize_t copied, len;

	/* Make sure to retain the original errno value in case of failure. */
	errno_saved = errno;

	/*
	 * If this was for a read and the read call succeeded, copy out the
	 * resulting data.
	 */
	if (op == _VECTORIO_READ && r > 0) {
		assert(buffer != NULL);
		copied = 0;
		i = 0;
		while (copied < r) {
			assert(i < iovcnt);
			len = iov[i].iov_len;
			if (len > r - copied)
				len = r - copied;
			memcpy(iov[i++].iov_base, buffer + copied, len);
			copied += len;
		}
		assert(r < 0 || r == copied);
	}

	/* Free the temporary buffer. */
	if (buffer != NULL)
		free(buffer);

	errno = errno_saved;
}

/*
 * Read a vector.
 */
ssize_t
readv(int fd, const struct iovec * iov, int iovcnt)
{
	char *ptr;
	ssize_t r;

	/*
	 * There ought to be just a readv system call here.  Instead, we use an
	 * intermediate buffer.  This approach is preferred over multiple read
	 * calls, because the actual I/O operation has to be atomic.
	 */
	if ((r = _vectorio_setup(iov, iovcnt, &ptr, _VECTORIO_READ, -1,
	    0)) <= 0)
		return r;

	r = read(fd, ptr, r);

	_vectorio_cleanup(iov, iovcnt, ptr, r, _VECTORIO_READ);

	return r;
}

/*
 * Read a vector at an offset, without changing the file offset.
 */
ssize_t
preadv(int fd, const struct iovec * iov, int iovcnt, off_t offset)
{
	char *ptr;
	ssize_t r;

	/* One pread(2) through a buffer, so that the I/O stays atomic. */
	if ((r = _vectorio_setup(iov, iovcnt, &ptr, _VECTORIO_READ, -1,
	    0)) <= 0)
		return r;

	r = pread(fd, ptr, r, offset);

	_vectorio_cleanup(iov, iovcnt, ptr, r, _VECTORIO_READ);

	return r;
}

/*
 * Write a vector at an offset, without changing the file offset.
 */
ssize_t
pwritev(int fd, const struct iovec * iov, int iovcnt, off_t offset)
{
	char *ptr;
	ssize_t r;

	if ((r = _vectorio_setup(iov, iovcnt, &ptr, _VECTORIO_WRITE, -1,
	    0)) <= 0)
		return r;

	r = pwrite(fd, ptr, r, offset);

	_vectorio_cleanup(iov, iovcnt, ptr, r, _VECTORIO_WRITE);

	return r;
}

/*
 * Write a vector.
 */
ssize_t
writev(int fd, const struct iovec * iov, int iovcnt)
{
	char *ptr;
	ssize_t r;

	/*
	 * There ought to be just a writev system call here.  Instead, we use
	 * an intermediate buffer.  This approach is preferred over multiple
	 * write calls, because the actual I/O operation has to be atomic.
	 */
	if ((r = _vectorio_setup(iov, iovcnt, &ptr, _VECTORIO_WRITE, fd,
	    0)) <= 0)
		return r;

	r = write(fd, ptr, r);

	_vectorio_cleanup(iov, iovcnt, ptr, r, _VECTORIO_WRITE);

	return r;
}
