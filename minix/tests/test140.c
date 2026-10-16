/* Test 140 - exec places the stack frame and mmap() at random addresses.
 *
 * The test execs itself several times.  Each child checks that its
 * arguments and environment came through intact (the stack frame, whose
 * pointers exec relocates, moved), and reports the address of a local
 * variable and of an anonymous mmap() through a pipe.  The parent expects
 * more than one distinct value of each: with 14 bits each, eight equal
 * values happen by chance once in 2^98.
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

#define RUNS	8

struct report {
	unsigned long stack, map;
	int ok;
};

static int
child(int fd, char **argv)
{
	struct report r;
	int here;
	void *m;

	r.ok = argv[2] != NULL && strcmp(argv[2], "second arg") == 0 &&
	    argv[4] == NULL && getenv("T140") != NULL &&
	    strcmp(getenv("T140"), "an environment string") == 0;
	m = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE,
	    -1, 0);
	r.stack = (unsigned long)&here;
	r.map = (m == MAP_FAILED) ? 0 : (unsigned long)m;
	if (write(fd, &r, sizeof(r)) != sizeof(r))
		return 1;
	return 0;
}

static int
distinct(unsigned long *v, int n)
{
	int i, j, d = 0;

	for (i = 0; i < n; i++) {
		for (j = 0; j < i; j++)
			if (v[j] == v[i])
				break;
		if (j == i)
			d++;
	}
	return d;
}

int
main(int argc, char **argv)
{
	char self[PATH_MAX], fdstr[16];
	unsigned long stacks[RUNS], maps[RUNS];
	struct report r;
	int p[2], i, status;
	pid_t pid;

	/* The child: self "child" "second arg" FD */
	if (argc == 4 && strcmp(argv[1], "child") == 0)
		return child(atoi(argv[3]), argv);

	if (realpath(argv[0], self) == NULL) {
		printf("cannot resolve %s\n", argv[0]);
		return 1;
	}
	start(140);
	setenv("T140", "an environment string", 1);

	for (i = 0; i < RUNS; i++) {
		subtest = 1;
		if (pipe(p) < 0) e(1);
		snprintf(fdstr, sizeof(fdstr), "%d", p[1]);
		if ((pid = fork()) == 0) {
			close(p[0]);
			execl(self, self, "child", "second arg", fdstr, (char *)NULL);
			_exit(127);
		}
		close(p[1]);
		memset(&r, 0, sizeof(r));
		if (read(p[0], &r, sizeof(r)) != sizeof(r)) e(2);
		close(p[0]);
		if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
		    WEXITSTATUS(status) != 0) e(3);
		if (!r.ok) e(4);
		stacks[i] = r.stack;
		maps[i] = r.map;
	}

	subtest = 2;
	if (distinct(stacks, RUNS) < 2) e(1);
	if (distinct(maps, RUNS) < 2) e(2);
	for (i = 0; i < RUNS; i++)
		if (maps[i] == 0) e(3);

	quit();
	return 0;
}
