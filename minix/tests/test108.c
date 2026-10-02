/* Test 108 - getentropy(3), preadv(2) and pwritev(2). */
#include <sys/types.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

static void
test_getentropy(void)
{
	unsigned char a[GETENTROPY_MAX], b[GETENTROPY_MAX];
	unsigned char zero[GETENTROPY_MAX];

	subtest = 1;
	memset(a, 0, sizeof(a));
	memset(b, 0, sizeof(b));
	memset(zero, 0, sizeof(zero));
	if (getentropy(a, sizeof(a)) != 0) e(1);
	if (getentropy(b, sizeof(b)) != 0) e(2);
	/* 256 random bytes: neither all zero nor twice the same. */
	if (memcmp(a, zero, sizeof(a)) == 0) e(3);
	if (memcmp(a, b, sizeof(a)) == 0) e(4);
	if (getentropy(a, 0) != 0) e(5);
	if (getentropy(a, GETENTROPY_MAX + 1) != -1 || errno != EINVAL)
		e(6);
	/* No descriptor left behind. */
	{
		int fd = dup(0), fd2;

		if (getentropy(a, 16) != 0) e(7);
		if ((fd2 = dup(0)) != fd + 1) e(8);
		close(fd);
		close(fd2);
	}
}

static void
test_pv(void)
{
	char buf[64], p1[4], p2[6], p3[5];
	struct iovec iov[3];
	int fd;

	subtest = 2;
	if ((fd = open("pvfile", O_RDWR | O_CREAT | O_TRUNC, 0644)) < 0)
		e(1);
	if (write(fd, "0123456789abcdefghij", 20) != 20) e(2);
	if (lseek(fd, 3, SEEK_SET) != 3) e(3);

	/* Gather-write at offset 5, leaving the file offset alone. */
	iov[0].iov_base = "AB";	iov[0].iov_len = 2;
	iov[1].iov_base = "CDE";	iov[1].iov_len = 3;
	if (pwritev(fd, iov, 2, 5) != 5) e(4);
	if (lseek(fd, 0, SEEK_CUR) != 3) e(5);
	memset(buf, 0, sizeof(buf));
	if (pread(fd, buf, 20, 0) != 20) e(6);
	if (memcmp(buf, "01234ABCDEabcdefghij", 20) != 0) e(7);

	/* Scatter-read at offset 2. */
	iov[0].iov_base = p1;	iov[0].iov_len = sizeof(p1);
	iov[1].iov_base = p2;	iov[1].iov_len = sizeof(p2);
	iov[2].iov_base = p3;	iov[2].iov_len = sizeof(p3);
	if (preadv(fd, iov, 3, 2) != 15) e(8);
	if (memcmp(p1, "234A", 4) || memcmp(p2, "BCDEab", 6) ||
	    memcmp(p3, "cdefg", 5)) e(9);
	if (lseek(fd, 0, SEEK_CUR) != 3) e(10);

	/* Short at end of file; nothing past it. */
	if (preadv(fd, iov, 3, 15) != 5) e(11);
	if (memcmp(p1, "fghi", 4) || p2[0] != 'j') e(12);
	if (preadv(fd, iov, 3, 20) != 0) e(13);

	/* Errors. */
	if (preadv(fd, iov, -1, 0) != -1 || errno != EINVAL) e(14);
	if (pwritev(fd, iov, 3, -1) != -1 || errno != EINVAL) e(15);
	if (preadv(-1, iov, 1, 0) != -1 || errno != EBADF) e(16);
	close(fd);
	unlink("pvfile");
}

int
main(int argc, char **argv)
{
	start(108);

	test_getentropy();
	test_pv();

	quit();
	return 0;
}
