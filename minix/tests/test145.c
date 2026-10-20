/* Test 145 - large and sparse address spaces under memory pressure.
 *
 * Since processes have the whole 47-bit address space, one process can ask VM
 * for far more bookkeeping than the machine has memory:
 *
 * Subtest 1 reserves 2 TB with one PROT_NONE mmap().  VM keeps a pointer per
 * page of a region, in its own heap; that heap grew without bound into the
 * window where VM maps its slabs and page tables, and VM then panicked (a slab
 * double free, "PT_PT(pt, pde)" in pt_ptalloc_in_range).  The mmap() may fail
 * with ENOMEM now, but VM must survive it.
 *
 * Subtest 2 maps preallocated single pages 2 MB apart, so that every page
 * needs a page table of its own, until memory runs out.  That ran VM's
 * mapping window out of space, and with a larger window it reached memory
 * pressure, where page table changes nest (allocating a page table may
 * compress pages of the same process) and the nested one let the process run
 * again: a kernel panic, "RTS_ISSET(p, RTS_VMINHIBIT)" in do_vmctl, on SMP.
 *
 * Each subtest runs in a child that may fail an mmap(), die of a fault or be
 * killed by the OOM killer; that must cost the child, not the system.  The
 * parent then checks that VM still hands out memory.  The child is a freshly
 * exec'd copy of this program: a forked one would share the parent's pages
 * copy-on-write, and the parent would die of its next write fault once the
 * child had used up memory (user faults fail below the OOM reserve).
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

#define RESERVE		(2UL << 40)		/* subtest 1: 2 TB */
#define STRIDE		(2UL * 1024 * 1024)	/* subtest 2: a table per page */
#define MAX_MAPS	(1024UL * 1024)
#define SPARSE_BASE	((char *)(16UL << 40))	/* free: below the mmap area */

static size_t pagesz;

static void
no_core(void)
{
	struct rlimit rl;

	/* A core dump of a sparse terabyte would be a test of its own. */
	rl.rlim_cur = rl.rlim_max = 0;
	(void)setrlimit(RLIMIT_CORE, &rl);
}

static void
reserve_child(void)
{
	char *p;

	no_core();
	p = mmap(NULL, RESERVE, PROT_NONE, MAP_ANON | MAP_PRIVATE, -1, 0);
	if (p == MAP_FAILED)
		_exit(errno == ENOMEM ? 0 : 1);
	/* Use a page of it. */
	if (mprotect(p + RESERVE / 2, pagesz, PROT_READ | PROT_WRITE) != 0)
		_exit(errno == ENOMEM ? 0 : 2);
	p[RESERVE / 2] = 'x';
	if (munmap(p, RESERVE) != 0)
		_exit(3);
	_exit(0);
}

static void
sparse_child(void)
{
	unsigned long i;

	no_core();
	for (i = 0; i < MAX_MAPS; i++) {
		if (mmap(SPARSE_BASE + i * STRIDE, pagesz,
		    PROT_READ | PROT_WRITE,
		    MAP_ANON | MAP_PRIVATE | MAP_FIXED | MAP_PREALLOC,
		    -1, 0) == MAP_FAILED)
			break;
	}
	_exit(0);
}

static void
run(const char *self, const char *child)
{
	pid_t pid;
	char *p;
	int status;

	if ((pid = fork()) == -1) e(1);
	if (pid == 0) {
		execl(self, self, child, (char *)NULL);
		_exit(1);
	}
	if (waitpid(pid, &status, 0) != pid) e(2);
	if (WIFEXITED(status) && WEXITSTATUS(status) != 0) e(3);

	if ((p = mmap(NULL, pagesz, PROT_READ | PROT_WRITE,
	    MAP_ANON | MAP_PRIVATE, -1, 0)) == MAP_FAILED) {
		e(4);
		return;
	}
	p[0] = 'x';
	if (p[0] != 'x') e(5);
	if (munmap(p, pagesz) != 0) e(6);
}

int
main(int argc, char **argv)
{
	char self[PATH_MAX];

	pagesz = (size_t) sysconf(_SC_PAGESIZE);

	if (argc == 2 && strcmp(argv[1], "reserve") == 0)
		reserve_child();
	if (argc == 2 && strcmp(argv[1], "sparse") == 0)
		sparse_child();
	/* start() moves to a directory of its own. */
	if (realpath(argv[0], self) == NULL) {
		printf("realpath %s: %s\n", argv[0], strerror(errno));
		return 1;
	}

	start(145);

	subtest = 1;
	run(self, "reserve");
	subtest = 2;
	run(self, "sparse");

	quit();

	return 0;
}
