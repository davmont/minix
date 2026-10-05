/* Test 127 - page table changes reach all threads of a process.
 *
 * The threads of a process share its page tables.  When VM unmaps a page or
 * takes away write access, a thread running on another CPU must not go on
 * using a stale TLB entry: its very next access has to fault, not one some
 * time later when the thread happens to be switched out.  Only meaningful
 * with more than one CPU; on one it passes trivially.
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

#define ROUNDS		50
#define STALE_ACCESSES	10000	/* this many after the change: stale TLB */

static volatile int *page;
static volatile int started, stop, faulted, writing;
static volatile unsigned long accesses;

static void
on_segv(int sig)
{
	/* Give the thread a page again, so that it can go on. */
	if (writing)
		(void)mprotect((void *)page, getpagesize(),
		    PROT_READ | PROT_WRITE);
	else
		(void)mmap((void *)page, getpagesize(), PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
	faulted = 1;
}

static void *
hammer(void *arg)
{

	started = 1;
	while (!stop) {
		if (writing)
			page[0]++;
		else
			(void)page[0];
		accesses++;
	}
	return NULL;
}

static void
test_change(int write)
{
	pthread_t t;
	unsigned long before;
	time_t t0;
	int r;

	writing = write;
	t0 = time(NULL);
	for (r = 0; r < ROUNDS; r++) {
		page = mmap(NULL, getpagesize(), PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANON, -1, 0);
		if (page == MAP_FAILED) e(1);
		page[0] = 1;			/* fault it in */
		started = stop = faulted = 0;
		accesses = 0;
		if (pthread_create(&t, NULL, hammer, NULL) != 0) e(2);
		while (!started)
			sched_yield();
		/* Let the thread run with its TLB entry loaded. */
		for (before = accesses; accesses - before < 100000; )
			sched_yield();

		if (write) {
			if (mprotect((void *)page, getpagesize(),
			    PROT_READ) != 0) e(3);
		} else {
			if (munmap((void *)page, getpagesize()) != 0) e(3);
		}

		/* From here on, the thread's next access must fault. */
		before = accesses;
		while (!faulted && accesses - before < STALE_ACCESSES)
			sched_yield();
		if (!faulted) {
			printf("round %d: %lu accesses after the change, "
			    "no fault\n", r, accesses - before);
			e(4);
		}
		stop = 1;
		if (pthread_join(t, NULL) != 0) e(5);
		(void)munmap((void *)page, getpagesize());
		/* With one CPU each round takes a few time slices: bound it. */
		if (errct > 0 || time(NULL) - t0 > 10) break;
	}
}

int
main(int argc, char **argv)
{
	struct sigaction sa;

	start(127);

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_segv;
	if (sigaction(SIGSEGV, &sa, NULL) != 0) e(1);

	subtest = 1;
	test_change(0);		/* munmap */
	subtest = 2;
	test_change(1);		/* mprotect to read-only */

	quit();
	return 0;
}
