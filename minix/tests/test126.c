/* Test 126 - path arguments are copied by VFS.
 *
 * The C library passes paths by address only and VFS copies the string, a
 * little at a time and never across a page boundary in one go: a bad
 * address fails with EFAULT instead of crashing the caller, and a path that
 * ends right before an unmapped page, or crosses a page boundary, works.
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

#define BAD	((const char *)0xdeadc0deUL)

static void
test_efault(void)
{
	struct stat st;
	char buf[16];
	int fd;

	subtest = 1;
	/* NULL and an unmapped address, through each way of passing a path. */
	if (open(NULL, O_RDONLY) != -1 || errno != EFAULT) e(1);
	if (open(BAD, O_RDONLY) != -1 || errno != EFAULT) e(2);
	if (stat(NULL, &st) != -1 || errno != EFAULT) e(3);
	if (stat(BAD, &st) != -1 || errno != EFAULT) e(4);
	if (mkdir(BAD, 0755) != -1 || errno != EFAULT) e(5);
	if (unlink(BAD) != -1 || errno != EFAULT) e(6);
	if (chmod(BAD, 0644) != -1 || errno != EFAULT) e(7);
	if (chown(BAD, 0, 0) != -1 || errno != EFAULT) e(8);
	if (truncate(BAD, 0) != -1 || errno != EFAULT) e(9);
	if (link(BAD, "x126") != -1 || errno != EFAULT) e(10);
	/* The first name is looked up before the second is fetched. */
	if ((fd = open("x126", O_RDWR | O_CREAT, 0644)) < 0) e(19);
	close(fd);
	if (link("x126", BAD) != -1 || errno != EFAULT) e(11);
	if (rename("x126", BAD) != -1 || errno != EFAULT) e(20);
	if (unlink("x126") != 0) e(21);
	if (rename(BAD, "x126") != -1 || errno != EFAULT) e(12);
	if (symlink(BAD, "x126") != -1 || errno != EFAULT) e(13);
	if (symlink("x126", BAD) != -1 || errno != EFAULT) e(14);
	if (readlink(BAD, buf, sizeof(buf)) != -1 || errno != EFAULT) e(15);
	if (mknod(BAD, S_IFIFO | 0644, 0) != -1 || errno != EFAULT) e(16);
	if (access(BAD, F_OK) != -1 || errno != EFAULT) e(17);
	if (chdir(BAD) != -1 || errno != EFAULT) e(18);
}

static void
test_boundaries(void)
{
	char *page, *p, *big;
	long pagesz = sysconf(_SC_PAGESIZE);
	struct stat st;
	int fd;

	subtest = 2;
	/* Two pages, the second one unmapped: a path ending at the very end
	 * of the first must be read without touching the second.
	 */
	page = mmap(NULL, 2 * pagesz, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANON, -1, 0);
	if (page == MAP_FAILED) e(1);
	if (munmap(page + pagesz, pagesz) != 0) e(2);
	p = page + pagesz - sizeof("f126");
	strcpy(p, "f126");
	if ((fd = open(p, O_RDWR | O_CREAT, 0644)) < 0) e(3);
	close(fd);
	if (stat(p, &st) != 0) e(4);
	/* One byte later the terminator would be in the unmapped page. */
	memmove(p + 1, p, sizeof("f126") - 1);
	if (stat(p + 1, &st) != -1 || errno != EFAULT) e(5);

	subtest = 3;
	/* A path that crosses from one page into the next. */
	big = mmap(NULL, 2 * pagesz, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANON, -1, 0);
	if (big == MAP_FAILED) e(1);
	p = big + pagesz - 3;
	strcpy(p, "./f126");
	if (stat(p, &st) != 0) e(2);
	if (unlink(p) != 0) e(3);
	munmap(big, 2 * pagesz);
	munmap(page, pagesz);

	subtest = 4;
	/* PATH_MAX: with its terminator a path takes at most PATH_MAX bytes. */
	if ((big = malloc(PATH_MAX + 2)) == NULL) e(1);
	memset(big, 'a', PATH_MAX + 1);
	big[PATH_MAX] = '\0';			/* PATH_MAX chars: too long */
	if (stat(big, &st) != -1 || errno != ENAMETOOLONG) e(2);
	memset(big, '/', PATH_MAX - 1);
	big[PATH_MAX - 1] = '\0';		/* PATH_MAX - 1 slashes: fine */
	if (stat(big, &st) != 0) e(3);
	if (stat("", &st) != -1 || errno != ENOENT) e(4);
	free(big);
}

int
main(int argc, char **argv)
{

	start(126);

	test_efault();
	test_boundaries();

	quit();
	return 0;
}
