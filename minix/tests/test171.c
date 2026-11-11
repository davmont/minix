/* Test 171 - MFS and mmap(2): the end of a file, and its access time.
 *
 * A shared mapping may write past the end of its file, in the last page,
 * which is the file's last block.  Those bytes are not the file's: once the
 * file grows over them, by ftruncate(2) or by a write(2) past its end, they
 * read as zeroes, through read(2) and through the mapping alike.  A write
 * through a shared mapping is a reference to the file, and updates its
 * access time.
 *
 * Both are the file system's doing: the test skips when its directory is not
 * on MFS.
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

static long pg;

/* A file of half a page of 'a's, and a shared mapping of it with 'b' written
 * at 'off' past its end.  Returns the descriptor; *pp is the mapping. */
static int
setup(const char *name, char **pp, off_t off)
{
	char buf[4096];
	char *p;
	int fd;

	(void)unlink(name);
	if ((fd = open(name, O_RDWR | O_CREAT | O_EXCL, 0600)) < 0) e(90);
	memset(buf, 'a', sizeof(buf));
	if (write(fd, buf, pg / 2) != pg / 2) e(91);
	if ((p = mmap(NULL, pg / 2, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
	    0)) == MAP_FAILED) e(92);
	if (p[pg / 2 - 1] != 'a') e(93);
	p[off] = 'b';
	*pp = p;
	return fd;
}

static char
byte_at(int fd, off_t off)
{
	char c;

	if (pread(fd, &c, 1, off) != 1) e(94);
	return c;
}

static void
test171a(void)
{
	char *p;
	int fd;

	subtest = 1;

	/* ftruncate(2) up, after the mapping is gone. */
	fd = setup("f1", &p, pg / 2 + 1);
	if (munmap(p, pg / 2) != 0) e(1);
	if (ftruncate(fd, pg) != 0) e(2);
	if (byte_at(fd, pg / 2 + 1) != 0) e(3);
	if (byte_at(fd, pg / 2 - 1) != 'a') e(4);
	close(fd);

	/* A write(2) past the end, the mapping still there. */
	fd = setup("f2", &p, pg / 2 + 1);
	if (pwrite(fd, "z", 1, pg - 1) != 1) e(5);
	if (byte_at(fd, pg / 2 + 1) != 0) e(6);
	if (p[pg / 2 + 1] != 0) e(7);		/* the mapping too */
	if (byte_at(fd, pg - 1) != 'z') e(8);
	if (munmap(p, pg / 2) != 0) e(9);
	close(fd);

	/* ftruncate(2) up, the mapping still there. */
	fd = setup("f3", &p, pg - 1);
	if (ftruncate(fd, pg) != 0) e(10);
	if (p[pg - 1] != 0 || byte_at(fd, pg - 1) != 0) e(11);
	if (munmap(p, pg / 2) != 0) e(12);
	close(fd);
}

static void
test171b(void)
{
	struct timeval tv[2];
	struct stat st;
	char *p;
	int fd;

	subtest = 2;

	(void)unlink("f4");
	if ((fd = open("f4", O_RDWR | O_CREAT | O_EXCL, 0600)) < 0) e(1);
	if (write(fd, "data", 4) != 4) e(2);
	/* An access a while ago, the file's last change. */
	if (gettimeofday(&tv[0], NULL) != 0) e(3);
	tv[0].tv_sec -= 100;
	tv[1] = tv[0];
	if (utimes("f4", tv) != 0) e(4);

	if ((p = mmap(NULL, 4, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
	    0)) == MAP_FAILED) e(5);
	p[0] = 'D';
	if (munmap(p, 4) != 0) e(6);
	if (fsync(fd) != 0) e(7);
	if (fstat(fd, &st) != 0) e(8);
	if (st.st_atime <= tv[0].tv_sec) e(9);
	if (byte_at(fd, 0) != 'D') e(10);
	close(fd);
}

int
main(int argc, char **argv)
{
	struct statvfs sv;

	start(171);
	pg = sysconf(_SC_PAGESIZE);

	if (statvfs(".", &sv) != 0) e(0);
	if (strcmp(sv.f_fstypename, "mfs") != 0) {
		printf("test171: not on MFS but %s, skipped\n",
		    sv.f_fstypename);
		quit();
	}

	test171a();
	test171b();

	quit();
	return 0;
}
