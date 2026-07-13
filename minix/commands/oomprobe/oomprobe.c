/*	oomprobe - shared memory must not be a way around the OOM defences
 *
 * A POSIX shm pool is anonymous memory held by the IPC server and vm_remap()ed
 * into its clients.  That ownership is what made it a hole:
 *
 *   - the fault that allocates a pool page runs with the IPC server -- a system
 *     service -- as the region's owner, so the OOM reserve (PAF_USERMEM), which
 *     keeps the last pages for system services, did not apply.  A user process
 *     could reach those pages simply by asking a service to allocate for it.
 *
 *   - RLIMIT_AS is enforced in do_mmap(), but an shm mapping reaches memory
 *     through do_remap() instead, so it escaped the limit entirely.
 *
 * Both are closed by tagging the pool VR_USERMEM.  This probe is what says so:
 * it checks that a pool is bounded by RLIMIT_AS, and that a client which tries
 * to eat all of memory through shm is stopped with the system still alive --
 * still able to fork, exec and allocate.
 *
 * Exits 0 only if every check passes.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MB		(1024UL * 1024UL)
#define POOL_BIG	(128UL * MB)
#define AS_LIMIT	(32UL * MB)

/*
 * The hog grabs memory in chunks rather than one huge pool, for two reasons.
 *
 * A single enormous pool does not test what it looks like it tests: the pool
 * lives in the IPC server's address space, which stops at USR_DATATOP (896 MB),
 * so an oversized mmap() is refused for want of address space long before it
 * reaches a single page of the OOM reserve.  It would "pass" without ever
 * exercising the thing under test.
 *
 * Chunks also let the hog run against whatever memory the machine has: it keeps
 * taking pools until one is refused, and reports how far it got.  Run it on a
 * machine with less RAM than that 896 MB ceiling and the reserve -- not the
 * address space -- is what has to stop it.
 */
#define HOG_CHUNK	(32UL * MB)
#define HOG_CHUNKS	24		/* 768 MB, under the IPC server's ceiling */

static int failures;

static void
check(const char *what, int ok, const char *detail)
{
	if (ok) {
		printf("%-46s -> OK\n", what);
	} else {
		printf("%-46s -> FAIL (%s)\n", what, detail);
		failures++;
	}
	fflush(stdout);
}

/* Make an shm object of the given size and return its fd. */
static int
make_pool(const char *name, size_t size)
{
	int fd;

	(void)shm_unlink(name);
	if ((fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600)) < 0)
		return -1;
	if (ftruncate(fd, size) != 0) {
		close(fd);
		(void)shm_unlink(name);
		return -1;
	}
	return fd;
}

/*
 * The child: take shm pools and touch every page of each, as a runaway client
 * would, until something refuses.  It must NOT get through all of them -- the
 * reserve has to stop it, either by refusing a mapping or by failing the fault
 * that would allocate a page (which kills the child with SIGSEGV).  Either way
 * the client is what dies, not the IPC server.
 *
 * Exits with the number of chunks it managed to take, so the parent can say how
 * far it got; HOG_CHUNKS means it took everything and the hole is still open.
 */
static void
hog(void)
{
	volatile unsigned char *p;
	char name[32];
	size_t off;
	int i, fd;

	for (i = 0; i < HOG_CHUNKS; i++) {
		snprintf(name, sizeof(name), "/oomprobe-hog%d", i);

		if ((fd = make_pool(name, HOG_CHUNK)) < 0)
			_exit(i);

		p = mmap(NULL, HOG_CHUNK, PROT_READ | PROT_WRITE, MAP_SHARED,
		    fd, 0);
		close(fd);
		if (p == MAP_FAILED)
			_exit(i);	/* refused: the reserve held */

		/* Fault every page in.  If the allocation behind one of these
		 * faults fails, we die here -- which is the intended outcome. */
		for (off = 0; off < HOG_CHUNK; off += 4096)
			p[off] = 1;
	}

	_exit(HOG_CHUNKS);		/* took it all: shm still reaches the reserve */
}

int
main(void)
{
	struct rlimit rl, saved;
	pid_t pid;
	int status, fd;
	void *p;

	printf("oomprobe: shm must not evade RLIMIT_AS or the OOM reserve\n\n");

	/* 1. Without a limit, a large pool maps fine.  It costs no RAM yet: the
	 * pages are faulted in on first touch, not at mmap(). */
	if ((fd = make_pool("/oomprobe-base", POOL_BIG)) < 0) {
		check("shm_open + ftruncate(128 MB)", 0, strerror(errno));
		return 1;
	}
	check("shm_open + ftruncate(128 MB)", 1, "");

	p = mmap(NULL, POOL_BIG, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	check("unlimited: mmap 128 MB pool", p != MAP_FAILED, strerror(errno));
	if (p != MAP_FAILED)
		(void)munmap(p, POOL_BIG);
	close(fd);
	(void)shm_unlink("/oomprobe-base");

	/* 2. Now bound the address space.  A pool that would blow past
	 * RLIMIT_AS must be refused -- this is the check do_mmap() applies and
	 * that the shm path used to slip around entirely. */
	if (getrlimit(RLIMIT_AS, &saved) != 0) {
		check("getrlimit(RLIMIT_AS)", 0, strerror(errno));
		return 1;
	}
	check("getrlimit(RLIMIT_AS)", 1, "");

	rl.rlim_cur = AS_LIMIT;
	rl.rlim_max = AS_LIMIT;
	check("setrlimit(RLIMIT_AS, 32 MB)",
	    setrlimit(RLIMIT_AS, &rl) == 0, strerror(errno));

	if ((fd = make_pool("/oomprobe-lim", POOL_BIG)) < 0) {
		check("shm_open under the limit", 0, strerror(errno));
		return 1;
	}

	errno = 0;
	p = mmap(NULL, POOL_BIG, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	check("RLIMIT_AS bounds an shm pool (128 MB > 32 MB)",
	    p == MAP_FAILED, "pool escaped RLIMIT_AS");
	if (p != MAP_FAILED)
		(void)munmap(p, POOL_BIG);
	close(fd);
	(void)shm_unlink("/oomprobe-lim");

	/* 3. Lift the limit again; the same pool must map. */
	check("setrlimit(RLIMIT_AS, unlimited)",
	    setrlimit(RLIMIT_AS, &saved) == 0, strerror(errno));

	if ((fd = make_pool("/oomprobe-again", POOL_BIG)) >= 0) {
		p = mmap(NULL, POOL_BIG, PROT_READ | PROT_WRITE, MAP_SHARED,
		    fd, 0);
		check("limit lifted: the pool maps again", p != MAP_FAILED,
		    strerror(errno));
		if (p != MAP_FAILED)
			(void)munmap(p, POOL_BIG);
		close(fd);
		(void)shm_unlink("/oomprobe-again");
	}

	/*
	 * 4. The real thing: a client that tries to take every page of memory
	 * through shm.  The reserve must stop it, and the system must still be
	 * standing afterwards.
	 */
	printf("\n  (running the hog: taking shm in %lu MB chunks, up to %lu MB,\n"
	    "   touching every page -- the reserve must stop it)\n",
	    HOG_CHUNK / MB, (HOG_CHUNK * HOG_CHUNKS) / MB);
	fflush(stdout);

	if ((pid = fork()) < 0) {
		check("fork the hog", 0, strerror(errno));
		return 1;
	}
	if (pid == 0)
		hog();

	if (waitpid(pid, &status, 0) != pid) {
		check("waitpid(hog)", 0, strerror(errno));
		return 1;
	}

	if (WIFSIGNALED(status))
		printf("%-46s -> killed by signal %d (a fault could not be "
		    "satisfied)\n", "  hog stopped", WTERMSIG(status));
	else if (WIFEXITED(status))
		printf("%-46s -> after %lu MB\n", "  hog stopped",
		    (unsigned long)(WEXITSTATUS(status) * (HOG_CHUNK / MB)));

	/*
	 * Taking every chunk means memory was there for the taking, and shm is
	 * still a way past the reserve.  Being killed mid-fault, or refused a
	 * mapping, both mean it was stopped.
	 */
	check("the hog did NOT get all of memory",
	    !(WIFEXITED(status) && WEXITSTATUS(status) == HOG_CHUNKS),
	    "shm still reaches the OOM reserve");

	for (int i = 0; i < HOG_CHUNKS; i++) {
		char name[32];

		snprintf(name, sizeof(name), "/oomprobe-hog%d", i);
		(void)shm_unlink(name);
	}

	/*
	 * 5. And this is what the reserve is *for*: after all that, the system
	 * can still fork, exec and allocate.  If shm had drained the last pages,
	 * these are exactly the things that would fail.
	 */
	if ((pid = fork()) < 0) {
		check("system survives: fork still works", 0, strerror(errno));
	} else if (pid == 0) {
		execl("/bin/echo", "echo", "-n", "", (char *)NULL);
		_exit(30);		/* exec failed */
	} else {
		(void)waitpid(pid, &status, 0);
		check("system survives: fork+exec still works",
		    WIFEXITED(status) && WEXITSTATUS(status) == 0,
		    "could not exec after the hog");
	}

	p = mmap(NULL, 4 * MB, PROT_READ | PROT_WRITE,
	    MAP_ANON | MAP_PRIVATE, -1, 0);
	check("system survives: anonymous memory again",
	    p != MAP_FAILED, strerror(errno));
	if (p != MAP_FAILED) {
		memset(p, 0x5a, 4 * MB);	/* really fault it in */
		(void)munmap(p, 4 * MB);
	}

	printf("\noomprobe: %s\n", failures == 0 ? "ALL PASS" : "FAILURES PRESENT");
	return failures == 0 ? 0 : 1;
}
