/* Test 142 - the user address space beyond 1 GB (amd64).
 *
 * A process has 128 TB, but for 896 MB-1 GB, which every address space keeps
 * for the kernel.  Its stack and mmap()s are high; VM makes the page table
 * levels above a page directory as they are first needed, shares them among
 * the threads of a process, copies them on fork() and frees them on exit.
 *
 *  1. the stack and an mmap() are above 4 GB;
 *  2. MAP_FIXED at 2 GB, 1 TB and 100 TB: written, then copy-on-write
 *     across fork() both ways, then munmap();
 *  3. MAP_FIXED in 896 MB-1 GB fails;
 *  4. an 8 GB region, touched along its whole length;
 *  5. threads fault their own pages in a fresh 1 TB-apart area;
 *  6. 100 children mapping high pages do not leak memory.
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

#define PG	4096UL
#define GB	(1UL << 30)
#define TB	(1UL << 40)

static void *
map_at(uintptr_t a, size_t len)
{
	void *p = mmap((void *)a, len, PROT_READ | PROT_WRITE,
	    MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);

	return p == MAP_FAILED ? NULL : p;
}

static void
test_layout(void)
{
	int here;
	void *m;

	subtest = 1;
	if ((uintptr_t)&here < 4 * GB) e(1);
	m = mmap(NULL, PG, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE,
	    -1, 0);
	if (m == MAP_FAILED) e(2);
	else if ((uintptr_t)m < 4 * GB) e(3);
	else munmap(m, PG);
}

static void
test_fixed(void)
{
	static const uintptr_t at[] = { 2 * GB, 1 * TB, 100 * TB };
	unsigned i;
	pid_t pid;
	int status;
	char *p;

	for (i = 0; i < sizeof(at) / sizeof(at[0]); i++) {
		subtest = 2;
		if ((p = map_at(at[i], 2 * PG)) == NULL) { e(1); continue; }
		if (p != (char *)at[i]) e(2);
		memset(p, 'a' + i, 2 * PG);
		if ((pid = fork()) == 0) {
			int bad = (p[0] != 'a' + (int)i ||
			    p[2 * PG - 1] != 'a' + (int)i);
			memset(p, 'z', 2 * PG);	/* the child's copy only */
			_exit(bad || p[PG] != 'z');
		}
		if (pid < 0 || waitpid(pid, &status, 0) != pid) { e(3); continue; }
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) e(4);
		if (p[0] != 'a' + (int)i || p[2 * PG - 1] != 'a' + (int)i) e(5);
		if (munmap(p, 2 * PG) != 0) e(6);
	}

	subtest = 3;
	if (map_at(0x38000000UL, PG) != NULL) e(1);
	if (map_at(0x3ffff000UL, 2 * PG) != NULL) e(2);	/* across 1 GB */
}

static void
test_big(void)
{
	size_t len = 8 * GB;
	char *p;
	size_t off;

	subtest = 4;
	p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE,
	    -1, 0);
	if (p == MAP_FAILED) { e(1); return; }
	for (off = 0; off < len; off += 256 * 1024 * 1024)
		p[off] = (char)(off >> 28);
	p[len - 1] = 'e';
	for (off = 0; off < len; off += 256 * 1024 * 1024)
		if (p[off] != (char)(off >> 28)) { e(2); break; }
	if (p[len - 1] != 'e') e(3);
	if (munmap(p, len) != 0) e(4);
}

#define THREADS	4
static char *tbase;

static void *
toucher(void *arg)
{
	uintptr_t n = (uintptr_t)arg;
	char *p = tbase + n * TB;

	/* Each thread faults in a page TB apart: its own PDPT entry. */
	memset(p, (int)n + 1, PG);
	return (void *)(uintptr_t)(p[PG - 1] == (char)(n + 1));
}

static void
test_threads(void)
{
	pthread_t t[THREADS];
	uintptr_t n;
	void *ok;
	unsigned i;

	subtest = 5;
	tbase = (char *)(20 * TB);
	for (i = 0; i < THREADS; i++)
		if (map_at((uintptr_t)tbase + i * TB, PG) == NULL) e(1);
	for (n = 0; n < THREADS; n++)
		if (pthread_create(&t[n], NULL, toucher, (void *)n) != 0) e(2);
	for (n = 0; n < THREADS; n++) {
		if (pthread_join(t[n], &ok) != 0) e(3);
		else if (!ok) e(4);
	}
	/* The main thread sees what the threads wrote. */
	for (i = 0; i < THREADS; i++)
		if (tbase[i * TB] != (char)(i + 1)) e(5);
	for (i = 0; i < THREADS; i++)
		munmap(tbase + i * TB, PG);
}

static void
test_leak(void)
{
	uint32_t total, free0, free1, cached;
	int i, status;
	pid_t pid;

	subtest = 6;
	getmem(&total, &free0, &cached);
	for (i = 0; i < 100; i++) {
		if ((pid = fork()) == 0) {
			char *a = map_at(3 * TB, PG), *b = map_at(50 * TB, PG),
			    *c = map_at(5 * GB, PG);
			if (a) *a = 1;
			if (b) *b = 1;
			if (c) *c = 1;
			_exit(!(a && b && c));
		}
		if (pid < 0 || waitpid(pid, &status, 0) != pid) { e(1); break; }
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) e(2);
	}
	getmem(&total, &free1, &cached);
	if (free1 + 128 < free0) e(3);
}

int
main(int argc, char **argv)
{

	start(142);
#if defined(__x86_64__)
	test_layout();
	test_fixed();
	test_big();
	test_threads();
	test_leak();
#endif
	quit();
	return 0;
}
