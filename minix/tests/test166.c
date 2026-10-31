/* Test 166 - mmap(2), munmap(2) and shm_open(3): the errors POSIX asks for.
 *
 * mmap() fails with EINVAL for both MAP_SHARED and MAP_PRIVATE (neither, a
 * bare MAP_FILE, is taken as private), and for an offset or MAP_FIXED address
 * off a page boundary; with EBADF without a descriptor; with EACCES for a file
 * not open for reading.
 * /dev/zero maps as anonymous memory.  munmap() fails with EINVAL for an
 * address off a page boundary and for a length of zero.  shm_open() sets
 * FD_CLOEXEC, refuses "." and "..", and says ENAMETOOLONG for a name too
 * long, whole or in part.
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

static void
test166a(void)
{
	long pg = sysconf(_SC_PAGESIZE);
	char *p;
	int fd;

	subtest = 1;

	if ((fd = open("f", O_RDWR | O_CREAT | O_TRUNC, 0600)) < 0) e(1);
	if (ftruncate(fd, 4 * pg) != 0) e(2);

	if (mmap(NULL, pg, PROT_READ, MAP_SHARED | MAP_PRIVATE, fd, 0) !=
	    MAP_FAILED || errno != EINVAL) e(3);
	if ((p = mmap(NULL, pg, PROT_READ, MAP_FILE, fd, 0)) == MAP_FAILED)
		e(4);
	if (munmap(p, pg) != 0) e(4);
	if (mmap(NULL, pg, PROT_READ, MAP_SHARED, fd, 1) != MAP_FAILED ||
	    errno != EINVAL) e(5);
	if (mmap(NULL, pg, PROT_READ, MAP_PRIVATE, -1, 0) != MAP_FAILED ||
	    errno != EBADF) e(6);

	/* A page-aligned offset works, and MAP_FIXED on a page. */
	if ((p = mmap(NULL, pg, PROT_READ, MAP_SHARED, fd, pg)) == MAP_FAILED)
		e(7);
	if (mmap(p + 1, pg, PROT_READ, MAP_SHARED | MAP_FIXED, fd, 0) !=
	    MAP_FAILED || errno != EINVAL) e(8);
	if (munmap(p, pg) != 0) e(9);
	if (mmap(p, pg, PROT_READ, MAP_SHARED | MAP_FIXED, fd, 0) != p) e(9);

	if (munmap(p + 1, pg) != -1 || errno != EINVAL) e(10);
	if (munmap(p, 0) != -1 || errno != EINVAL) e(11);
	if (munmap((void *)-1, pg) != -1 || errno != EINVAL) e(12);
	if (munmap(p, pg) != 0) e(13);
	close(fd);

	/* Write-only: no mapping at all. */
	if ((fd = open("f", O_WRONLY)) < 0) e(14);
	if (mmap(NULL, pg, PROT_READ, MAP_PRIVATE, fd, 0) != MAP_FAILED ||
	    errno != EACCES) e(15);
	close(fd);
	unlink("f");

	/* /dev/zero: a private mapping, which needs it open only for reading
	 * (and the tests run as bin, who may not write it). */
	if ((fd = open("/dev/zero", O_RDONLY)) < 0) e(16);
	if ((p = mmap(NULL, pg, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0)) ==
	    MAP_FAILED) e(17);
	else {
		if (p[0] != 0 || p[pg - 1] != 0) e(18);
		p[0] = 1;
		if (munmap(p, pg) != 0) e(19);
	}
	close(fd);

	/* The anonymous mappings VM's own callers make still work. */
	if ((p = mmap(NULL, pg, PROT_READ | PROT_WRITE, MAP_ANON, -1, 0)) ==
	    MAP_FAILED) e(20);
	if (munmap(p, pg) != 0) e(21);
}

static void
test166b(void)
{
	char name[PATH_MAX + 1];
	int fd, flags;

	subtest = 2;

	shm_unlink("/t166");
	if ((fd = shm_open("/t166", O_RDWR | O_CREAT | O_EXCL, 0600)) < 0)
		e(1);
	if ((flags = fcntl(fd, F_GETFD)) < 0 || !(flags & FD_CLOEXEC)) e(2);
	close(fd);
	if (shm_unlink("/t166") != 0) e(3);

	if (shm_open("..", O_RDWR | O_CREAT, 0600) != -1 || errno != EINVAL)
		e(4);
	if (shm_open("/.", O_RDWR | O_CREAT, 0600) != -1 || errno != EINVAL)
		e(5);

	memset(name, 'a', sizeof(name) - 1);
	name[0] = '/';
	name[sizeof(name) - 1] = '\0';
	if (shm_open(name, O_RDWR | O_CREAT, 0600) != -1 ||
	    errno != ENAMETOOLONG) e(6);
	if (shm_unlink(name) != -1 || errno != ENAMETOOLONG) e(7);
	name[NAME_MAX + 2] = '\0';	/* one component, NAME_MAX + 1 long */
	if (shm_open(name, O_RDWR | O_CREAT, 0600) != -1 ||
	    errno != ENAMETOOLONG) e(8);
}

int
main(int argc, char **argv)
{
	start(166);

	test166a();
	test166b();

	quit();
	return 0;
}
