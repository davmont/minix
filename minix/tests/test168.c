/* Test 168 - mmap(MAP_SHARED | MAP_ANON): memory shared across fork(2).
 *
 * Parent and child see each other's writes to such a mapping, both to pages
 * touched before the fork and to pages first touched after it, by either of
 * them, even once the one that wrote has exited.  This holds for the parts
 * that mprotect(2) and munmap(2) leave of a mapping, and down to grandchildren.
 * MAP_PRIVATE anonymous memory is still copied on write.
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

#define NPAGES	8

static long pg;

static char *
map(int flags)
{
	char *p;

	p = mmap(NULL, NPAGES * pg, PROT_READ | PROT_WRITE, flags | MAP_ANON,
	    -1, 0);
	if (p == MAP_FAILED) e(90);
	return p;
}

/* Wait for child 'pid', which must have exited with 0. */
static void
reap(pid_t pid)
{
	int status;

	if (waitpid(pid, &status, 0) != pid) e(91);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) e(92);
}

/* A child writes, to pages the parent did and did not touch, and exits. */
static void
test168a(void)
{
	char *p;
	pid_t pid;
	int i;

	subtest = 1;

	p = map(MAP_SHARED);
	for (i = 0; i < NPAGES; i += 2)
		p[i * pg] = 'p';		/* the even pages only */

	if ((pid = fork()) == -1) e(1);
	if (pid == 0) {
		for (i = 0; i < NPAGES; i++)
			if (p[i * pg] != (i % 2 ? 0 : 'p')) _exit(1);
		for (i = 0; i < NPAGES; i++)
			p[i * pg + 1] = 'c';
		_exit(0);
	}
	reap(pid);

	for (i = 0; i < NPAGES; i++)
		if (p[i * pg + 1] != 'c') e(2);
	if (munmap(p, NPAGES * pg) != 0) e(3);
}

/* Both write after the fork, in turn, to pages touched or not before it. */
static void
test168b(void)
{
	char *p, c;
	pid_t pid;
	int up[2], down[2];

	subtest = 2;

	p = map(MAP_SHARED);
	p[0] = 'x';
	if (pipe(up) != 0 || pipe(down) != 0) e(1);

	if ((pid = fork()) == -1) e(2);
	if (pid == 0) {
		if (read(down[0], &c, 1) != 1) _exit(1);
		if (p[0] != 'P' || p[pg] != 'Q') _exit(2);
		p[0] = 'C';
		p[2 * pg] = 'D';
		if (write(up[1], "", 1) != 1) _exit(3);
		_exit(0);
	}
	p[0] = 'P';
	p[pg] = 'Q';
	if (write(down[1], "", 1) != 1) e(3);
	if (read(up[0], &c, 1) != 1) e(4);
	if (p[0] != 'C' || p[2 * pg] != 'D') e(5);
	reap(pid);

	close(up[0]); close(up[1]); close(down[0]); close(down[1]);
	if (munmap(p, NPAGES * pg) != 0) e(6);
}

/* A grandchild writes after its parent, the one in between, has exited. */
static void
test168c(void)
{
	char *p;
	pid_t pid, gpid;
	int fd[2], i;

	subtest = 3;

	p = map(MAP_SHARED);
	if (pipe(fd) != 0) e(1);

	if ((pid = fork()) == -1) e(2);
	if (pid == 0) {
		if ((gpid = fork()) == -1) _exit(1);
		if (gpid == 0) {
			char c;

			/* Until the child is gone. */
			close(fd[1]);
			if (read(fd[0], &c, 1) != 0) _exit(1);
			if (p[pg] != 'm') _exit(2);
			p[2 * pg] = 'g';
			_exit(0);
		}
		p[pg] = 'm';
		_exit(0);
	}
	close(fd[0]);
	reap(pid);
	close(fd[1]);

	/* The grandchild is not ours to wait for: poll for its write. */
	for (i = 0; p[2 * pg] != 'g'; i++) {
		if (i == 1000) e(3);
		usleep(10000);
	}
	if (p[pg] != 'm') e(4);
	if (munmap(p, NPAGES * pg) != 0) e(5);
}

/* What mprotect(2) and munmap(2) leave of a mapping is still shared, at the
 * same offsets. */
static void
test168d(void)
{
	char *p;
	pid_t pid;
	int i;

	subtest = 4;

	p = map(MAP_SHARED);
	/* Split it in three: pages 0-1, 2-3 and 4-7. */
	if (mprotect(p + 2 * pg, 2 * pg, PROT_READ) != 0) e(1);
	p[0] = 'a';

	if ((pid = fork()) == -1) e(2);
	if (pid == 0) {
		/* Shrink from the start, and leave a hole. */
		if (munmap(p, pg) != 0) _exit(1);
		if (munmap(p + 5 * pg, pg) != 0) _exit(2);
		p[pg] = 'b';
		if (mprotect(p + 2 * pg, 2 * pg, PROT_READ | PROT_WRITE) != 0)
			_exit(3);
		p[3 * pg] = 'd';
		p[4 * pg] = 'e';
		p[7 * pg] = 'h';
		_exit(0);
	}
	reap(pid);

	if (p[0] != 'a' || p[pg] != 'b' || p[2 * pg] != 0 ||
	    p[3 * pg] != 'd' || p[4 * pg] != 'e' || p[5 * pg] != 0 ||
	    p[6 * pg] != 0 || p[7 * pg] != 'h') e(3);

	/* Read-only, but mprotect(2) may make it writable, as anonymous. */
	if (mprotect(p + 2 * pg, 2 * pg, PROT_READ | PROT_WRITE) != 0) e(4);
	p[2 * pg] = 'c';
	for (i = 0; i < NPAGES; i++)
		p[i * pg + 1] = '0' + i;
	if ((pid = fork()) == -1) e(5);
	if (pid == 0) {
		for (i = 0; i < NPAGES; i++)
			if (p[i * pg + 1] != '0' + i) _exit(1);
		_exit(p[2 * pg] == 'c' ? 0 : 2);
	}
	reap(pid);
	if (munmap(p, NPAGES * pg) != 0) e(6);
}

/* Several children, each its own slot; and private memory is still private. */
static void
test168e(void)
{
	char *p, *q;
	pid_t pid[4];
	int i;

	subtest = 5;

	p = map(MAP_SHARED);
	q = map(MAP_PRIVATE);
	q[0] = 'q';
	for (i = 0; i < 4; i++) {
		if ((pid[i] = fork()) == -1) e(1);
		if (pid[i] == 0) {
			p[i * pg + 3] = 'A' + i;
			q[0] = 'Q';
			q[pg] = 'R';
			_exit(0);
		}
	}
	for (i = 0; i < 4; i++)
		reap(pid[i]);
	for (i = 0; i < 4; i++)
		if (p[i * pg + 3] != 'A' + i) e(2);
	if (q[0] != 'q' || q[pg] != 0) e(3);
	if (munmap(p, NPAGES * pg) != 0 || munmap(q, NPAGES * pg) != 0) e(4);
}

int
main(int argc, char **argv)
{

	start(168);
	pg = sysconf(_SC_PAGESIZE);

	test168a();
	test168b();
	test168c();
	test168d();
	test168e();

	quit();
	return 0;
}
