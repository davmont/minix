/* Test 115 - posix_fallocate(2).
 *
 * Storage for the range is allocated (the file system's free block count
 * drops by the blocks in the range; MFS's st_blocks ignores holes, so it
 * cannot show this), holes are filled,
 * the file grows to cover the range but never shrinks, existing data is kept,
 * and errors come back as the return value with errno untouched.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

#define FILE1	"fafile"

/* Free blocks on the file system holding 'fd', and its block size. */
static fsblkcnt_t
bfree(int fd, off_t *bsp)
{
	struct statvfs sv;

	if (fsync(fd) != 0) e(89);
	if (fstatvfs(fd, &sv) != 0) e(90);
	if (bsp != NULL) *bsp = sv.f_frsize;
	return sv.f_bfree;
}

static off_t
size(int fd)
{
	struct stat st;

	if (fstat(fd, &st) != 0) e(91);
	return st.st_size;
}

static int
newfile(void)
{
	int fd;

	unlink(FILE1);
	if ((fd = open(FILE1, O_RDWR | O_CREAT | O_TRUNC, 0644)) < 0) e(92);
	return fd;
}

static void
test_alloc(void)
{
	fsblkcnt_t before;
	char buf[16];
	off_t bs;
	int fd;

	subtest = 1;
	/* A new file grows, with storage behind every byte. */
	fd = newfile();
	before = bfree(fd, &bs);
	if (posix_fallocate(fd, 0, 100000) != 0) e(1);
	if (size(fd) != 100000) e(2);
	if ((before - bfree(fd, NULL)) * bs < 100000) e(3);
	if (pread(fd, buf, sizeof(buf), 99990) != 10) e(4);
	if (memcmp(buf, "\0\0\0\0\0\0\0\0\0\0", 10) != 0) e(5);

	subtest = 2;
	/* Data in the range is kept; a range inside the file does not
	 * change its size.
	 */
	if (pwrite(fd, "abc", 3, 0) != 3) e(1);
	if (pwrite(fd, "xyz", 3, 50000) != 3) e(2);
	if (posix_fallocate(fd, 0, 60000) != 0) e(3);
	if (size(fd) != 100000) e(4);
	if (pread(fd, buf, 3, 0) != 3 || memcmp(buf, "abc", 3) != 0) e(5);
	if (pread(fd, buf, 3, 50000) != 3 || memcmp(buf, "xyz", 3) != 0) e(6);
	/* A range past the end extends the file to its end. */
	if (posix_fallocate(fd, 200000, 1) != 0) e(7);
	if (size(fd) != 200001) e(8);
	close(fd);
}

static void
test_holes(void)
{
	fsblkcnt_t before;
	off_t bs;
	int fd;

	subtest = 3;
	/* A sparse file gets its holes filled; its size stays. */
	fd = newfile();
	before = bfree(fd, &bs);
	if (ftruncate(fd, 64 * bs) != 0) e(1);
	if (before - bfree(fd, NULL) > 4) e(2);	/* it is sparse */
	before = bfree(fd, NULL);
	if (posix_fallocate(fd, 0, 64 * bs) != 0) e(3);
	if (before - bfree(fd, NULL) < 64) e(4);
	if (size(fd) != 64 * bs) e(5);
	/* Again: nothing left to allocate. */
	before = bfree(fd, NULL);
	if (posix_fallocate(fd, 0, 64 * bs) != 0) e(6);
	if (bfree(fd, NULL) != before) e(7);
	close(fd);

	subtest = 4;
	/* A small range across a block boundary allocates both blocks. */
	fd = newfile();
	if (ftruncate(fd, 64 * bs) != 0) e(1);
	before = bfree(fd, NULL);
	if (posix_fallocate(fd, 8 * bs - 10, 20) != 0) e(2);
	if (before - bfree(fd, NULL) < 2) e(3);
	close(fd);
	unlink(FILE1);
}

static void
test_errors(void)
{
	int fd, p[2], saved;

	subtest = 5;
	fd = newfile();
	errno = 1234;
	if (posix_fallocate(fd, 0, 0) != EINVAL) e(1);
	if (posix_fallocate(fd, -1, 10) != EINVAL) e(2);
	if (posix_fallocate(fd, INT64_MAX - 10, 100) != EFBIG) e(3);
	if (posix_fallocate(-1, 0, 10) != EBADF) e(4);
	if (errno != 1234) e(5);		/* errno is left alone */
	close(fd);

	if ((fd = open(FILE1, O_RDONLY)) < 0) e(6);
	if (posix_fallocate(fd, 0, 10) != EBADF) e(7);
	close(fd);

	if (pipe(p) != 0) e(8);
	if (posix_fallocate(p[1], 0, 10) != ESPIPE) e(9);
	close(p[0]);
	close(p[1]);

	if ((fd = open("/dev/null", O_WRONLY)) < 0) e(10);
	if (posix_fallocate(fd, 0, 10) != ENODEV) e(11);
	close(fd);

	subtest = 6;
	/* Running out of space is reported, not ignored. */
	fd = newfile();
	saved = posix_fallocate(fd, 0, (off_t)4 << 30);
	if (saved != ENOSPC && saved != EFBIG) e(1);
	close(fd);
	unlink(FILE1);
}

int
main(int argc, char **argv)
{

	start(115);

	test_alloc();
	test_holes();
	test_errors();

	quit();
	return 0;
}
