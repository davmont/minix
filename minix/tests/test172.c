/* Test 172 - mlock(2), munlock(2), mlockall(2), munlockall(2), and
 * RLIMIT_MEMLOCK.
 *
 * What is locked shows in msync(MS_INVALIDATE), which fails with EBUSY on
 * locked pages.  Locks split regions and are kept per page; a fork(2) child
 * does not inherit them; mlockall(MCL_FUTURE) locks later mappings.  Past
 * RLIMIT_MEMLOCK only the superuser may lock, so the test is setuid root, and
 * takes a user's effective ID for the limits.  After mlockall(MCL_FUTURE) the
 * heap grows locked, and a file mapping's pages are in before mmap() returns.
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

#define NOBODY	12345		/* an effective user ID that is not root's */

static long pg;

static char *
map_anon(size_t len, int prot)
{
	char *p;

	if ((p = mmap(NULL, len, prot, MAP_PRIVATE | MAP_ANON, -1, 0)) ==
	    MAP_FAILED) e(90);
	return p;
}

/* Is page 'p' locked?  msync(MS_INVALIDATE) is EBUSY on a locked page. */
static int
locked(void *p)
{

	if (msync(p, pg, MS_INVALIDATE) == 0)
		return 0;
	if (errno != EBUSY) e(91);
	return 1;
}

static void
test172a(void)
{
	char *p;
	int i;

	subtest = 1;

	p = map_anon(8 * pg, PROT_READ | PROT_WRITE);
	for (i = 0; i < 8; i++)
		p[i * pg] = 'a' + i;

	if (mlock(p, 0) != 0) e(1);
	if (locked(p)) e(2);
	/* Off a page: the pages it touches. */
	if (mlock(p + 1, pg) != 0) e(3);
	if (!locked(p) || !locked(p + pg) || locked(p + 2 * pg)) e(24);
	if (munlock(p + pg + 1, 1) != 0) e(25);		/* page 1 only */
	if (!locked(p) || locked(p + pg)) e(26);
	if (munlock(p + 1, 1) != 0 || locked(p)) e(27);

	/* The middle of the mapping: it splits, and the rest is not. */
	if (mlock(p + 2 * pg, 3 * pg) != 0) e(4);
	for (i = 0; i < 8; i++)
		if (locked(p + i * pg) != (i >= 2 && i < 5)) e(5);
	for (i = 0; i < 8; i++)
		if (p[i * pg] != 'a' + i) e(6);
	/* Locking again is no change; a part may be unlocked. */
	if (mlock(p + 2 * pg, 3 * pg) != 0) e(7);
	if (munlock(p + 3 * pg, pg) != 0) e(8);
	for (i = 0; i < 8; i++)
		if (locked(p + i * pg) != (i == 2 || i == 4)) e(9);
	if (munlock(p, 8 * pg) != 0) e(10);
	for (i = 0; i < 8; i++)
		if (locked(p + i * pg)) e(11);

	/* A hole in the range: ENOMEM, and nothing is locked. */
	if (munmap(p + 6 * pg, pg) != 0) e(12);
	if (mlock(p, 8 * pg) != -1 || errno != ENOMEM) e(13);
	if (locked(p)) e(14);
	if (munlock(p, 8 * pg) != -1 || errno != ENOMEM) e(15);

	/* Untouched pages, and pages no one may access. */
	if (munmap(p, 8 * pg) != 0) e(16);
	p = map_anon(4 * pg, PROT_READ | PROT_WRITE);
	if (mprotect(p + 2 * pg, 2 * pg, PROT_NONE) != 0) e(17);
	if (mlock(p, 4 * pg) != 0) e(18);
	if (p[0] != 0 || p[pg] != 0) e(19);
	p[pg] = 'x';
	if (!locked(p) || !locked(p + 3 * pg)) e(20);
	/* Unmapping ends the lock. */
	if (munmap(p, 4 * pg) != 0) e(21);
	p = map_anon(4 * pg, PROT_READ | PROT_WRITE);
	if (locked(p)) e(22);
	if (munmap(p, 4 * pg) != 0) e(23);
}

/* File mappings, shared and private, to a page past the end of the file. */
static void
test172b(void)
{
	char buf[4096], *s, *q;
	int fd;

	subtest = 2;

	memset(buf, 'f', sizeof(buf));
	(void)unlink("f1");
	if ((fd = open("f1", O_RDWR | O_CREAT | O_EXCL, 0600)) < 0) e(1);
	if (write(fd, buf, pg) != pg) e(2);
	if ((s = mmap(NULL, 2 * pg, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
	    0)) == MAP_FAILED) e(3);
	if ((q = mmap(NULL, pg, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd,
	    0)) == MAP_FAILED) e(4);

	/* The second page is past the end of the file: it is left out. */
	if (mlock(s, 2 * pg) != 0) e(5);
	if (mlock(q, pg) != 0) e(6);
	if (!locked(s) || !locked(q)) e(7);
	if (s[0] != 'f' || q[pg - 1] != 'f') e(8);
	q[0] = 'p';				/* private */
	s[1] = 's';				/* shared */
	if (pread(fd, buf, 2, 0) != 2 || buf[0] != 'f' || buf[1] != 's')
		e(9);

	if (munlock(s, 2 * pg) != 0 || munlock(q, pg) != 0) e(10);
	if (locked(s) || locked(q)) e(11);
	if (munmap(s, 2 * pg) != 0 || munmap(q, pg) != 0) e(12);
	close(fd);
	(void)unlink("f1");
}

/* A child of fork() has no locks, and its own do not touch its parent's. */
static void
test172c(void)
{
	char *p;
	pid_t pid;
	int status;

	subtest = 3;

	p = map_anon(2 * pg, PROT_READ | PROT_WRITE);
	p[0] = 'p';
	if (mlock(p, pg) != 0) e(1);

	if ((pid = fork()) == -1) e(2);
	if (pid == 0) {
		if (msync(p, pg, MS_INVALIDATE) != 0) _exit(1);
		if (p[0] != 'p') _exit(2);
		if (mlock(p + pg, pg) != 0) _exit(3);
		if (munlockall() != 0) _exit(4);
		_exit(0);
	}
	if (waitpid(pid, &status, 0) != pid) e(3);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) e(4);
	if (!locked(p) || locked(p + pg)) e(5);
	if (munmap(p, 2 * pg) != 0) e(6);
}

/* mlockall(), and munlockall(). */
static void
test172d(void)
{
	char *p, *q;

	subtest = 4;

	if (mlockall(0) != -1 || errno != EINVAL) e(1);
	if (mlockall(MCL_CURRENT | 0x100) != -1 || errno != EINVAL) e(2);

	p = map_anon(pg, PROT_READ | PROT_WRITE);
	if (mlockall(MCL_CURRENT) != 0) e(3);
	if (!locked(p)) e(4);
	q = map_anon(pg, PROT_READ | PROT_WRITE);	/* not MCL_FUTURE */
	if (locked(q)) e(5);
	if (munmap(q, pg) != 0) e(6);

	if (mlockall(MCL_FUTURE) != 0) e(7);
	q = map_anon(pg, PROT_READ | PROT_WRITE);
	if (!locked(q)) e(8);
	if (q[0] != 0) e(9);

	if (munlockall() != 0) e(10);
	if (locked(p) || locked(q)) e(11);
	if (munmap(q, pg) != 0) e(12);
	q = map_anon(pg, PROT_READ | PROT_WRITE);	/* MCL_FUTURE is off */
	if (locked(q)) e(13);
	if (munmap(p, pg) != 0 || munmap(q, pg) != 0) e(14);
}

/* RLIMIT_MEMLOCK, for a user that is not the superuser. */
static void
test172e(void)
{
	struct rlimit rl, orl;
	char *p;

	subtest = 5;

	if (geteuid() != 0) {
		printf("test172: not setuid root, subtest 5 skipped\n");
		return;
	}
	if (getrlimit(RLIMIT_MEMLOCK, &orl) != 0) e(1);
	if (orl.rlim_cur > orl.rlim_max) e(2);

	p = map_anon(8 * pg, PROT_READ | PROT_WRITE);
	rl.rlim_cur = 4 * pg;
	rl.rlim_max = 6 * pg;
	if (setrlimit(RLIMIT_MEMLOCK, &rl) != 0) e(3);
	if (getrlimit(RLIMIT_MEMLOCK, &rl) != 0) e(4);
	if (rl.rlim_cur != (rlim_t)(4 * pg) || rl.rlim_max != (rlim_t)(6 * pg))
		e(5);

	if (seteuid(NOBODY) != 0) e(6);
	if (mlock(p, 5 * pg) != -1 || errno != ENOMEM) e(7);
	if (locked(p)) e(8);
	if (mlock(p, 3 * pg) != 0) e(9);
	if (mlock(p + 3 * pg, pg) != 0) e(10);	/* just to the limit */
	if (mlock(p + 4 * pg, pg) != -1 || errno != ENOMEM) e(11);
	if (mlock(p, 2 * pg) != 0) e(12);	/* locked already: no more */
	if (mlockall(MCL_CURRENT) != -1 || errno != ENOMEM) e(13);

	/* A later mapping past the limit: EAGAIN. */
	if (mlockall(MCL_FUTURE) != 0) e(14);
	if (mmap(NULL, pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1,
	    0) != MAP_FAILED || errno != EAGAIN) e(15);
	/* The heap does not grow past it either. */
	if (sbrk(pg) != (void *)-1 || errno != ENOMEM) e(27);
	if (munlockall() != 0) e(16);

	/* The hard limit is the superuser's to raise. */
	rl.rlim_cur = 4 * pg;
	rl.rlim_max = 7 * pg;
	if (setrlimit(RLIMIT_MEMLOCK, &rl) != -1 || errno != EPERM) e(17);
	rl.rlim_cur = 6 * pg + 1;
	rl.rlim_max = 6 * pg;
	if (setrlimit(RLIMIT_MEMLOCK, &rl) != -1 || errno != EINVAL) e(18);
	rl.rlim_cur = rl.rlim_max = 0;
	if (setrlimit(RLIMIT_MEMLOCK, &rl) != 0) e(19);
	if (mlock(p, pg) != -1 || errno != EPERM) e(20);

	/* The superuser may lock past it. */
	if (seteuid(0) != 0) e(21);
	if (mlock(p, 8 * pg) != 0) e(22);
	if (!locked(p + 7 * pg)) e(23);
	if (munlockall() != 0) e(24);
	if (setrlimit(RLIMIT_MEMLOCK, &orl) != 0) e(25);
	if (munmap(p, 8 * pg) != 0) e(26);
}

/* After mlockall(MCL_FUTURE): the heap, and the pages of a file mapping. */
static void
test172f(void)
{
	char buf[4096], *b, *q;
	int fd;

	subtest = 6;

	if (mlockall(MCL_FUTURE) != 0) e(1);

	/* The heap grows locked. */
	b = sbrk(0);
	if ((uintptr_t)b % pg != 0 && sbrk(pg - (uintptr_t)b % pg) == (void *)-1)
		e(2);
	if ((b = sbrk(2 * pg)) == (void *)-1) e(3);
	if (!locked(b) || !locked(b + pg)) e(4);
	if (b[0] != 0 || b[2 * pg - 1] != 0) e(5);

	/* A private writable file mapping is brought in written to: it has
	 * its own copy of the page already, and a later write to the file
	 * does not show in it. */
	memset(buf, 'f', sizeof(buf));
	(void)unlink("f2");
	if ((fd = open("f2", O_RDWR | O_CREAT | O_EXCL, 0600)) < 0) e(6);
	if (write(fd, buf, pg) != pg) e(7);
	if ((q = mmap(NULL, pg, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd,
	    0)) == MAP_FAILED) e(8);
	if (!locked(q)) e(9);
	if (pwrite(fd, "n", 1, 0) != 1) e(10);
	if (q[0] != 'f') e(11);

	if (munlockall() != 0) e(12);
	if (locked(q) || locked(b)) e(13);
	if (munmap(q, pg) != 0) e(14);
	close(fd);
	(void)unlink("f2");
	if (sbrk(-2 * pg) == (void *)-1) e(15);
}

int
main(int argc, char **argv)
{

	start(172);
	pg = sysconf(_SC_PAGESIZE);

	test172a();
	test172b();
	test172c();
	test172d();
	test172e();
	test172f();

	quit();
	return 0;
}
