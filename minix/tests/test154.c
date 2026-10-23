/* Test 154 - processes faulting together on a shared mapping of a file.
 *
 * ld.so maps the first page of each library it loads MAP_SHARED to read its
 * ELF header.  A file system with no VM cache of its own (isofs, the root of
 * a live CD) gives VM one-time pages, and each fault on one asked it again;
 * the new page replaced the old one under the other shared mappings, which
 * then faulted and asked again in turn.  With a few processes starting
 * together on the same library, none of them ran any more (test 10 hung).
 *
 * Here children map the first page of libc, as ld.so does, read it and unmap
 * it, over and over, all at once.  On a live CD the library is on isofs.  A
 * child that gets stuck is killed by its alarm.
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

#define CHILDREN	8
#define ROUNDS		300
#define TIMEOUT		60	/* seconds, for each child */
#define LIBRARY		"/usr/lib/libc.so.12"

static void
child(int fd)
{
	unsigned char *p;
	int i;

	alarm(TIMEOUT);
	for (i = 0; i < ROUNDS; i++) {
		p = mmap(NULL, getpagesize(), PROT_READ, MAP_SHARED, fd, 0);
		if (p == MAP_FAILED) _exit(1);
		if (memcmp(p, "\177ELF", 4) != 0) _exit(2);
		if (munmap(p, getpagesize()) != 0) _exit(3);
		/* A fresh descriptor now and then, as ld.so has. */
		if (i % 10 == 9) {
			close(fd);
			if ((fd = open(LIBRARY, O_RDONLY)) < 0) _exit(4);
		}
	}
	_exit(0);
}

int
main(int argc, char **argv)
{
	pid_t pids[CHILDREN];
	int fd, i, status;

	start(154);

	if ((fd = open(LIBRARY, O_RDONLY)) < 0) {
		e(1);
		quit();
	}

	for (i = 0; i < CHILDREN; i++) {
		if ((pids[i] = fork()) == 0)
			child(fd);
		if (pids[i] < 0) e(2);
	}
	for (i = 0; i < CHILDREN; i++) {
		if (pids[i] <= 0) continue;
		if (waitpid(pids[i], &status, 0) != pids[i]) e(3);
		else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) e(4);
	}
	close(fd);

	quit();
	return 0;
}
