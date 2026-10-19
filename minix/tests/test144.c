/* Test 144 - character device I/O into file mappings.
 *
 * A read(2) from /dev/zero into a MAP_PRIVATE mapping of a file whose pages
 * are not in memory used to hang the system when that file lived on a RAM
 * disk: the memory driver serves both /dev/zero and the RAM disk, so its
 * copy into the buffer made VM wait for the file's pages, which waited for
 * the memory driver.  test74 did this, and it hung whenever the suite ran
 * from a RAM disk, as it does on the live CD.
 *
 * The test runs in the current directory, which the harness puts on a RAM
 * disk.  It flushes the file system cache before each operation so the
 * pages have to come back through the driver, and checks the results:
 * zeroes for the read into a private writable mapping, EFAULT for a read
 * into a read-only one, and the whole length for a write from a file mapping
 * to /dev/null.
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

#define NPAGES	10
#define FNAME	"t144file"

static size_t pagesz, len;

/* Open the file and drop its pages from the file system cache. */
static int
open_flushed(void)
{
	int fd;

	if ((fd = open(FNAME, O_RDONLY)) < 0) e(10);
	if (fcntl(fd, F_FLUSH_FS_CACHE) < 0) e(11);
	return fd;
}

static void
test_read_zero(void)
{
	char *p;
	size_t i;
	int fd, zfd;

	if ((zfd = open("/dev/zero", O_RDONLY)) < 0) e(1);

	/* Private and writable: the read succeeds and the pages are zero. */
	fd = open_flushed();
	p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (p == MAP_FAILED) e(2);
	if (read(zfd, p, len) != (ssize_t)len) e(3);
	for (i = 0; i < len; i++)
		if (p[i] != 0) { e(4); break; }
	if (munmap(p, len) != 0) e(5);
	if (close(fd) != 0) e(6);

	/* Read-only: the read fails with EFAULT and the file is intact. */
	fd = open_flushed();
	p = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
	if (p == MAP_FAILED) e(7);
	errno = 0;
	if (read(zfd, p, len) != -1 || errno != EFAULT) e(8);
	for (i = 0; i < len; i++)
		if (p[i] != 'a' + (char)(i / pagesz)) { e(9); break; }
	if (munmap(p, len) != 0) e(12);
	if (close(fd) != 0) e(13);

	if (close(zfd) != 0) e(14);
}

static void
test_write_null(void)
{
	char *p;
	int fd, nfd;

	if ((nfd = open("/dev/null", O_WRONLY)) < 0) e(20);

	fd = open_flushed();
	p = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) e(21);
	if (write(nfd, p, len) != (ssize_t)len) e(22);
	if (munmap(p, len) != 0) e(23);
	if (close(fd) != 0) e(24);

	if (close(nfd) != 0) e(25);
}

int
main(int argc, char **argv)
{
	char *buf;
	size_t i;
	int fd;

	start(144);

	pagesz = (size_t) sysconf(_SC_PAGESIZE);
	len = NPAGES * pagesz;

	if ((buf = malloc(len)) == NULL) e(90);
	for (i = 0; i < len; i++)
		buf[i] = 'a' + (char)(i / pagesz);
	if ((fd = open(FNAME, O_RDWR | O_CREAT | O_TRUNC, 0644)) < 0) e(91);
	if (write(fd, buf, len) != (ssize_t)len) e(92);
	if (fsync(fd) != 0) e(93);
	if (close(fd) != 0) e(94);
	free(buf);

	test_read_zero();
	test_write_null();

	quit();

	return 0;
}
