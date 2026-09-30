/* Test 95 - mprotect(2).
 *
 * Checks that protection changes are actually enforced by VM: a page made
 * read-only faults on write, a PROT_NONE page faults on any access, the rest
 * of the mapping is unaffected (the region is split at the range ends),
 * protection can be given back without losing the contents, it survives
 * fork(), kernel copies into a protected buffer are refused, and a kernel
 * copy into a page shared copy-on-write after fork() breaks the sharing,
 * and code cannot run from memory without execute permission (NX).
 */
#include <sys/mman.h>
#include <dlfcn.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>

#include "common.h"

#define PAGES	4

int max_error = 0;

static long pagesize;
static sigjmp_buf jb;
static volatile int faults;

static void
segv(int sig)
{
	faults++;
	siglongjmp(jb, 1);
}

/* Read or write one byte at p; return 1 if that faulted. */
static int
faults_on(volatile char *p, int write)
{
	int before = faults;
	char c;

	if (sigsetjmp(jb, 1) == 0) {
		if (write)
			*p = 'w';
		else
			c = *p;
		(void)c;
	}
	return faults != before;
}

static char *
map_pages(void)
{
	char *p;
	int i;

	p = mmap(NULL, PAGES * pagesize, PROT_READ | PROT_WRITE,
	    MAP_ANON | MAP_PRIVATE, -1, 0);
	if (p == MAP_FAILED) {
		e(1);
		quit();
	}
	for (i = 0; i < PAGES; i++)
		p[i * pagesize] = 'a' + i;
	return p;
}

/* Read-only middle page: writes fault, reads work, neighbours unaffected. */
static void
test_readonly(void)
{
	char *p;

	subtest = 1;
	p = map_pages();
	if (mprotect(p + pagesize, pagesize, PROT_READ) != 0) e(1);
	if (faults_on(p + pagesize, 0)) e(2);
	if (!faults_on(p + pagesize, 1)) e(3);
	if (p[pagesize] != 'b') e(4);
	if (faults_on(p, 1)) e(5);			/* split: still RW */
	if (faults_on(p + 2 * pagesize, 1)) e(6);
	if (mprotect(p + pagesize, pagesize, PROT_READ | PROT_WRITE) != 0)
		e(7);
	if (faults_on(p + pagesize, 1)) e(8);
	if (munmap(p, PAGES * pagesize) != 0) e(9);
}

/* PROT_NONE: every access faults; contents come back with the access. */
static void
test_none(void)
{
	char *p;

	subtest = 2;
	p = map_pages();
	if (mprotect(p + 2 * pagesize, 2 * pagesize, PROT_NONE) != 0) e(1);
	if (!faults_on(p + 2 * pagesize, 0)) e(2);
	if (!faults_on(p + 3 * pagesize, 1)) e(3);
	if (faults_on(p + pagesize, 0)) e(4);
	if (mprotect(p + 2 * pagesize, 2 * pagesize, PROT_READ) != 0) e(5);
	if (p[2 * pagesize] != 'c' || p[3 * pagesize] != 'd') e(6);
	if (!faults_on(p + 2 * pagesize, 1)) e(7);
	/* Pages never touched before PROT_NONE must fault too. */
	if (mprotect(p, PAGES * pagesize, PROT_NONE) != 0) e(8);
	if (!faults_on(p, 0)) e(9);
	if (munmap(p, PAGES * pagesize) != 0) e(10);
}

/* Argument checking. */
static void
test_errors(void)
{
	char *p;

	subtest = 3;
	p = map_pages();
	if (mprotect(p + 1, pagesize, PROT_READ) != -1 || errno != EINVAL)
		e(1);
	if (mprotect(p, pagesize, 0x100) != -1 || errno != EINVAL) e(2);
	if (munmap(p + pagesize, pagesize) != 0) e(3);
	/* A hole in the range: ENOMEM, and nothing changed. */
	if (mprotect(p, 3 * pagesize, PROT_READ) != -1 || errno != ENOMEM)
		e(4);
	if (faults_on(p, 1)) e(5);
	if (mprotect(p, 0, PROT_READ) != 0) e(6);
	if (munmap(p, PAGES * pagesize) != 0) e(7);
}

/* The protection is inherited across fork(). */
static void
test_fork(void)
{
	char *p;
	int status;
	pid_t pid;

	subtest = 4;
	p = map_pages();
	if (mprotect(p, pagesize, PROT_READ) != 0) e(1);
	pid = fork();
	if (pid == 0) {
		errct = 0;
		if (!faults_on(p, 1)) e(2);
		if (faults_on(p + pagesize, 1)) e(3);
		exit(errct ? 1 : 0);
	}
	if (pid < 0) e(4);
	if (waitpid(pid, &status, 0) != pid) e(5);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) e(6);
	if (munmap(p, PAGES * pagesize) != 0) e(7);
}

/*
 * A kernel copy into a buffer the caller may not write is refused with
 * EFAULT, and the buffer is left alone.
 */
#define COPY_REFUSED(r)	((r) == -1 && errno == EFAULT)

static void
test_kernel_copy(void)
{
	char *p;
	int fd;

	subtest = 5;
	p = map_pages();
	if ((fd = open("t95.data", O_RDWR | O_CREAT | O_TRUNC, 0644)) < 0) e(1);
	if (write(fd, "0123456789abcdef", 16) != 16) e(2);
	if (lseek(fd, 0, SEEK_SET) != 0) e(3);
	if (mprotect(p, pagesize, PROT_READ) != 0) e(4);
	if (!COPY_REFUSED(read(fd, p, 16))) e(5);
	if (p[0] != 'a') e(6);
	if (mprotect(p, pagesize, PROT_NONE) != 0) e(7);
	if (!COPY_REFUSED(write(fd, p, 16))) e(8);
	if (mprotect(p, pagesize, PROT_READ | PROT_WRITE) != 0) e(9);
	if (lseek(fd, 0, SEEK_SET) != 0) e(10);
	if (read(fd, p, 16) != 16 || p[0] != '0') e(11);
	close(fd);
	unlink("t95.data");
	if (munmap(p, PAGES * pagesize) != 0) e(12);
}

/* Static data, not just anonymous mmap memory. */
static char data_pages[3 * 65536] = { 1 };

static void
test_data(void)
{
	char *p;

	subtest = 6;
	p = (char *)(((unsigned long)data_pages + pagesize - 1) &
	    ~(pagesize - 1));
	p[0] = 'x';
	if (mprotect(p, pagesize, PROT_READ) != 0) e(1);
	if (!faults_on(p, 1)) e(2);
	if (p[0] != 'x') e(3);
	if (mprotect(p, pagesize, PROT_READ | PROT_WRITE) != 0) e(4);
	if (faults_on(p, 1)) e(5);
}

/*
 * After fork() both processes share their anonymous pages copy-on-write.
 * A read(2) by the child into such a page must break the sharing: it used
 * to be written straight into the frame the parent still maps, because the
 * kernel's copy path ignored the page's write protection.
 */
static void
test_cow_copy(void)
{
	char *p;
	int fd, status;
	pid_t pid;

	subtest = 7;
	p = map_pages();
	memset(p, 'P', pagesize);
	if ((fd = open("t95.cow", O_RDWR | O_CREAT | O_TRUNC, 0644)) < 0) e(1);
	if (write(fd, "childchildchild!", 16) != 16) e(2);
	pid = fork();
	if (pid == 0) {
		errct = 0;
		if (lseek(fd, 0, SEEK_SET) != 0) e(3);
		if (read(fd, p, 16) != 16) e(4);
		if (memcmp(p, "childchildchild!", 16) != 0) e(5);
		exit(errct ? 1 : 0);
	}
	if (pid < 0) e(6);
	if (waitpid(pid, &status, 0) != pid) e(7);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) e(8);
	if (p[0] != 'P' || p[15] != 'P') e(9);	/* parent's copy untouched */
	close(fd);
	unlink("t95.cow");
	if (munmap(p, PAGES * pagesize) != 0) e(10);
}

#if defined(__x86_64__) || defined(__i386__)
/* mov $42,%eax; ret */
static const unsigned char ret42[] = { 0xb8, 0x2a, 0x00, 0x00, 0x00, 0xc3 };

/* Call the code at p; return its result, or -1 if running it faulted. */
static int
run_code(void *p)
{
	int (*fn)(void) = (int (*)(void))p;
	int before = faults, r = -1;

	if (sigsetjmp(jb, 1) == 0)
		r = fn();
	return faults != before ? -1 : r;
}

/*
 * No-execute: code written to memory mapped without PROT_EXEC -- anonymous
 * mmap, the stack, the heap -- must not run; mprotect(PROT_EXEC) makes it
 * runnable.
 */
static void
test_noexec(void)
{
	unsigned char stackcode[64];
	unsigned char *p, *h;

	subtest = 8;
	p = map_pages();
	memcpy(p, ret42, sizeof(ret42));
	if (run_code(p) != -1) e(1);
	if (mprotect(p, pagesize, PROT_READ | PROT_EXEC) != 0) e(2);
	if (run_code(p) != 42) e(3);
	if (!faults_on(p, 1)) e(4);		/* read+exec: writes fault */
	if (mprotect(p, pagesize, PROT_READ | PROT_WRITE) != 0) e(5);
	if (run_code(p) != -1) e(6);
	if (munmap(p, PAGES * pagesize) != 0) e(7);

	memcpy(stackcode, ret42, sizeof(ret42));
	if (run_code(stackcode) != -1) e(8);

	if ((h = malloc(4 * pagesize)) == NULL) e(9);
	memcpy(h, ret42, sizeof(ret42));
	if (run_code(h) != -1) e(10);
	free(h);
}
#else
static void test_noexec(void) { }
#endif

/* mmap() itself honours the protection, as mprotect() does. */
static void
test_mmap_prot(void)
{
	char *p;

	subtest = 9;
	p = mmap(NULL, pagesize, PROT_READ, MAP_ANON | MAP_PRIVATE, -1, 0);
	if (p == MAP_FAILED) e(1);
	if (faults_on(p, 0)) e(2);
	if (!faults_on(p, 1)) e(3);
	if (munmap(p, pagesize) != 0) e(4);

	p = mmap(NULL, pagesize, PROT_NONE, MAP_ANON | MAP_PRIVATE, -1, 0);
	if (p == MAP_FAILED) e(5);
	if (!faults_on(p, 0)) e(6);
	if (mprotect(p, pagesize, PROT_READ | PROT_WRITE) != 0) e(7);
	if (faults_on(p, 1)) e(8);
	if (munmap(p, pagesize) != 0) e(9);
}

/* The dynamic linker maps a shared library's code read-only (for years
 * MINIX mapped every segment writable, lacking mprotect), and exec maps the
 * program's own text read-only: writing to either faults, running them
 * still works. */
static void
test_text_readonly(void)
{
	size_t (*libc_strlen)(const char *);

	subtest = 10;
	libc_strlen = (size_t (*)(const char *))dlsym(RTLD_DEFAULT, "strlen");
	if (libc_strlen == NULL) e(1);
	else {
		if (faults_on((volatile char *)(void *)libc_strlen, 0)) e(2);
		if (!faults_on((volatile char *)(void *)libc_strlen, 1)) e(3);
		if (libc_strlen("abc") != 3) e(4);
	}
	if (!faults_on((volatile char *)(void *)test_text_readonly, 1)) e(5);
}

int
main(int argc, char **argv)
{
	struct sigaction sa;

	start(95);

	pagesize = sysconf(_SC_PAGESIZE);
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = segv;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGSEGV, &sa, NULL) != 0) e(1);

	test_readonly();
	test_none();
	test_errors();
	test_fork();
	test_kernel_copy();
	test_data();
	test_cow_copy();
	test_noexec();
	test_mmap_prot();
	test_text_readonly();

	quit();
	return 0;
}
