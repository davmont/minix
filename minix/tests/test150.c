/* Test 150 - POSIX shared memory objects that are unlinked unmapped.
 *
 * The IPC server frees an unlinked object once no one maps it, but one never
 * mapped kept its slot for ever: after creating and unlinking 128 objects
 * without mapping them, shm_open() failed with ENOSPC until the reboot.
 *
 * An object unlinked while open, and mapped afterwards, is still one object
 * shared between the processes that map it, also after many others come and
 * go.
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

#define OBJECTS	1000		/* more than the IPC server has slots */

static char name[32];

/* Create and unlink objects without mapping them. */
static void
churn(int n)
{
	int i, fd;

	for (i = 0; i < n; i++) {
		snprintf(name, sizeof(name), "/t150_%d", i);
		(void)shm_unlink(name);
		fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd < 0) {
			e(1);
			return;
		}
		if (close(fd) != 0) e(2);
		if (shm_unlink(name) != 0) e(3);
	}
}

/* A child writes v into the mapping; the parent must see it. */
static void
child_writes(volatile int *p, int v)
{
	pid_t pid;
	int status;

	switch (pid = fork()) {
	case -1:
		e(4);
		return;
	case 0:
		*p = v;
		_exit(0);
	}
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0) e(5);
	if (*p != v) e(6);
}

static void
test150a(void)
{

	subtest = 1;
	churn(OBJECTS);
}

static void
test150b(void)
{
	volatile int *p;
	int fd;

	subtest = 2;
	(void)shm_unlink("/t150_mapped");
	fd = shm_open("/t150_mapped", O_RDWR | O_CREAT | O_EXCL, 0600);
	if (fd < 0) e(7);
	if (ftruncate(fd, getpagesize()) != 0) e(8);
	if (shm_unlink("/t150_mapped") != 0) e(9);
	p = mmap(NULL, getpagesize(), PROT_READ | PROT_WRITE, MAP_SHARED, fd,
	    0);
	if (p == MAP_FAILED) {
		e(10);
		return;
	}
	if (close(fd) != 0) e(11);

	child_writes(p, 1);
	churn(OBJECTS);			/* a mapped one is not reclaimed */
	child_writes(p, 2);

	if (munmap((void *)p, getpagesize()) != 0) e(12);
}

int
main(int argc, char **argv)
{

	start(150);

	test150a();
	test150b();

	quit();
	return 0;
}
