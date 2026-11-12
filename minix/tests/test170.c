/* Test 170 - mmap(2) limits, and shm_unlink(3) permissions.
 *
 * A MAP_FIXED mapping that does not fit in the address space fails with
 * ENOMEM, and leaves what was mapped there alone.  A process may have only so
 * many mappings: past that, mmap(2), and mprotect(2) of part of a mapping,
 * fail with ENOMEM, until it unmaps some.  shm_unlink(3) of another user's
 * object fails with EACCES.
 *
 * Must be setuid root.
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

static long pg;

/* A MAP_FIXED mapping that does not fit fails, before it unmaps anything. */
static void
test170a(void)
{
	static const size_t lens[] = { SIZE_MAX, SIZE_MAX / 2 + 1,
		(size_t)1 << 47 };
	char *p, *q;
	unsigned int i;
	int fd;

	subtest = 1;

	if ((p = mmap(NULL, 2 * pg, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANON, -1, 0)) == MAP_FAILED) e(1);
	p[0] = 'a';
	for (i = 0; i < sizeof(lens) / sizeof(lens[0]); i++) {
		errno = 0;
		q = mmap(p, lens[i] & ~(pg - 1), PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
		if (q != MAP_FAILED || errno != ENOMEM) e(2);
		if (p[0] != 'a') e(3);		/* still there */
	}

	/* The same over a shared memory object, as the POSIX suite does. */
	(void)shm_unlink("/t170");
	if ((fd = shm_open("/t170", O_RDWR | O_CREAT | O_EXCL, 0600)) < 0)
		e(4);
	if (shm_unlink("/t170") != 0) e(5);
	if (ftruncate(fd, 2 * pg) != 0) e(6);
	errno = 0;
	q = mmap(p, (SIZE_MAX / 2 + 1) & ~(pg - 1), PROT_READ | PROT_WRITE,
	    MAP_SHARED | MAP_FIXED, fd, 0);
	if (q != MAP_FAILED || errno != ENOMEM) e(7);
	if (p[0] != 'a') e(8);
	close(fd);
	if (munmap(p, 2 * pg) != 0) e(9);
}

/* Map pages one by one until mmap(2) says ENOMEM. */
#define MAXMAPS	70000

static void
test170b(void)
{
	char **maps, *three;
	int n, i;

	subtest = 2;

	if ((maps = calloc(MAXMAPS, sizeof(*maps))) == NULL) e(1);
	if ((three = mmap(NULL, 3 * pg, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANON, -1, 0)) == MAP_FAILED) e(2);

	for (n = 0; n < MAXMAPS; n++) {
		errno = 0;
		maps[n] = mmap(NULL, pg, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANON, -1, 0);
		if (maps[n] == MAP_FAILED)
			break;
	}
	if (n == MAXMAPS) e(3);			/* no limit */
	if (errno != ENOMEM) e(4);
	if (n < 60000) e(5);			/* about 65530, less our own */

	/* Splitting a mapping makes one more, and two more fail as well. */
	errno = 0;
	if (mprotect(three + pg, pg, PROT_READ) != -1 || errno != ENOMEM)
		e(6);
	three[pg] = 'w';			/* still writable */

	/* Room again. */
	for (i = 0; i < 4; i++)
		if (munmap(maps[--n], pg) != 0) e(7);
	if (mprotect(three + pg, pg, PROT_READ) != 0) e(8);
	if ((maps[n] = mmap(NULL, pg, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANON, -1, 0)) == MAP_FAILED) e(9);
	n++;

	while (n > 0)
		if (munmap(maps[--n], pg) != 0) e(10);
	if (munmap(three, 3 * pg) != 0) e(11);
	free(maps);
}

/* shm_unlink(3) of another user's object: EACCES. */
static void
test170c(void)
{
	uid_t ruid = getuid();
	int fd;

	subtest = 3;

	if (geteuid() != 0 || ruid == 0) {
		printf("test170c: needs to be setuid root, and run by a user\n");
		return;
	}
	(void)shm_unlink("/t170c");
	if ((fd = shm_open("/t170c", O_RDWR | O_CREAT | O_EXCL, 0666)) < 0)
		e(1);
	close(fd);
	if (seteuid(ruid) != 0) e(2);
	errno = 0;
	if (shm_unlink("/t170c") != -1 || errno != EACCES) e(3);
	if (seteuid(0) != 0) e(4);
	if (shm_unlink("/t170c") != 0) e(5);
}

int
main(int argc, char **argv)
{

	start(170);
	pg = sysconf(_SC_PAGESIZE);

	test170a();
	test170b();
	test170c();

	quit();
	return 0;
}
