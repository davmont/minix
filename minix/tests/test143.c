/* Test 143 - preallocated mappings that are not writable.
 *
 * MAP_PREALLOC makes VM fault in every page of a new mapping before mmap()
 * returns.  It used to fault them in as writes whatever the protection, and
 * VM asserts that a write fault never hits a read-only region, so any process
 * could panic VM, and with it the system, with one mmap(PROT_READ,
 * MAP_ANON | MAP_PREALLOC).  test74 did, by a typo, once it got that far.
 *
 * Subtest 1 maps read-only preallocated memory, reads it back as zeroes and
 * expects a write to it to kill a child with SIGSEGV.  Subtests 2 and 3 do
 * the same with PROT_NONE and with MAP_CONTIG, where either success or an
 * error is fine, as long as VM survives: every subtest ends with an ordinary
 * mapping that is written and read back.
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

#define NPAGES	4

static size_t pagesz;

static void
vm_alive(void)
{
	char *p;

	if ((p = mmap(NULL, pagesz, PROT_READ | PROT_WRITE,
	    MAP_ANON | MAP_PRIVATE, -1, 0)) == MAP_FAILED) e(90);
	p[0] = 'x';
	if (p[0] != 'x') e(91);
	if (munmap(p, pagesz) != 0) e(92);
}

static void
expect_segv(volatile char *p)
{
	pid_t pid;
	int status;

	if ((pid = fork()) == -1) e(80);
	if (pid == 0) {
		*p = 'x';
		_exit(0);
	}
	if (waitpid(pid, &status, 0) != pid) e(81);
	if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGSEGV) e(82);
}

static void
test_readonly(void)
{
	size_t len = NPAGES * pagesz, i;
	char *p;

	subtest = 1;

	if ((p = mmap(NULL, len, PROT_READ,
	    MAP_ANON | MAP_PRIVATE | MAP_PREALLOC, -1, 0)) == MAP_FAILED) e(1);
	else {
		for (i = 0; i < len; i++)
			if (p[i] != 0) { e(2); break; }
		expect_segv(p + len - 1);
		if (munmap(p, len) != 0) e(3);
	}
	vm_alive();
}

static void
test_noaccess(void)
{
	size_t len = NPAGES * pagesz;
	char *p;

	subtest = 2;

	if ((p = mmap(NULL, len, PROT_NONE,
	    MAP_ANON | MAP_PRIVATE | MAP_PREALLOC, -1, 0)) != MAP_FAILED) {
		expect_segv(p);
		if (munmap(p, len) != 0) e(1);
	}
	vm_alive();
}

static void
test_contig(void)
{
	size_t len = NPAGES * pagesz;
	char *p;

	subtest = 3;

	if ((p = mmap(NULL, len, PROT_READ,
	    MAP_ANON | MAP_PRIVATE | MAP_PREALLOC | MAP_CONTIG, -1, 0)) !=
	    MAP_FAILED) {
		if (p[0] != 0) e(1);
		if (munmap(p, len) != 0) e(2);
	}
	vm_alive();
}

int
main(int argc, char **argv)
{

	start(143);

	pagesz = (size_t) sysconf(_SC_PAGESIZE);

	test_readonly();
	test_noaccess();
	test_contig();

	quit();
	return 0;
}
