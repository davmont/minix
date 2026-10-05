/* Test 128 - shared mappings of files (MAP_SHARED), and msync.
 *
 * A shared mapping and the file are one and the same: what the mapping
 * writes, read(2) sees and other mappings see, and the other way around;
 * holes fill in; the mapping ends at the end of the file (SIGBUS beyond it);
 * write access needs a file open for writing.
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

#define FILE1	"t128a"
#define FILE2	"t128b"

static long pg;
static sigjmp_buf jb;
static volatile int sigs;

static void
on_bus(int sig)
{
	sigs = sig;
	siglongjmp(jb, 1);
}

/* Access a byte; return the signal it caused, or 0. */
static int
touch(volatile char *p, int write)
{
	sigs = 0;
	if (sigsetjmp(jb, 1) == 0) {
		if (write)
			*p = 'x';
		else
			(void)*p;
	}
	return sigs;
}

static int
make_file(const char *name, size_t pages, char fill)
{
	char *buf;
	int fd;

	if ((fd = open(name, O_RDWR | O_CREAT | O_TRUNC, 0644)) < 0) e(90);
	if ((buf = malloc(pg)) == NULL) e(91);
	memset(buf, fill, pg);
	while (pages-- > 0)
		if (write(fd, buf, pg) != pg) e(92);
	free(buf);
	return fd;
}

static char
byte_at(int fd, off_t off)
{
	char c = 0;

	if (pread(fd, &c, 1, off) != 1) e(93);
	return c;
}

static void
test_coherence(void)
{
	char *p, c;
	int fd;

	subtest = 1;
	fd = make_file(FILE1, 3, 'a');
	p = mmap(NULL, 3 * pg, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) e(1);
	if (p[0] != 'a' || p[2 * pg] != 'a') e(2);

	/* The mapping writes the file... */
	p[1] = 'b';
	p[pg + 7] = 'c';
	if (byte_at(fd, 1) != 'b') e(3);
	if (byte_at(fd, pg + 7) != 'c') e(4);

	/* ...and sees what is written to it. */
	c = 'd';
	if (pwrite(fd, &c, 1, 2 * pg + 5) != 1) e(5);
	if (p[2 * pg + 5] != 'd') e(6);
	/* A whole-block write as well, which need not read the block. */
	{
		char *buf = malloc(pg);
		memset(buf, 'e', pg);
		if (pwrite(fd, buf, pg, pg) != pg) e(7);
		free(buf);
	}
	if (p[pg] != 'e' || p[pg + 7] != 'e') e(8);
	p[pg + 9] = 'f';
	if (byte_at(fd, pg + 9) != 'f') e(9);

	/* It stays so across a flush: writing after it is not lost. */
	if (msync(p, 3 * pg, MS_SYNC) != 0) e(10);
	sync();
	p[3] = 'g';
	if (byte_at(fd, 3) != 'g') e(11);
	if (msync(p, 3 * pg, MS_ASYNC) != 0) e(12);
	if (munmap(p, 3 * pg) != 0) e(13);
	close(fd);

	/* And it is in the file afterwards. */
	if ((fd = open(FILE1, O_RDONLY)) < 0) e(14);
	if (byte_at(fd, 1) != 'b' || byte_at(fd, 3) != 'g' ||
	    byte_at(fd, pg + 9) != 'f' || byte_at(fd, 2 * pg + 5) != 'd') e(15);
	close(fd);
}

static void
test_sharing(void)
{
	char *p, *q;
	pid_t pid;
	int fd, fd2, status;

	subtest = 2;
	fd = make_file(FILE1, 2, 'a');
	p = mmap(NULL, 2 * pg, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) e(1);

	/* Inherited by a child: one mapping. */
	if ((pid = fork()) < 0) e(2);
	if (pid == 0) {
		p[10] = 'c';
		_exit(p[11] == 'a' ? 0 : 1);
	}
	if (waitpid(pid, &status, 0) != pid || status != 0) e(3);
	if (p[10] != 'c') e(4);

	/* Two mappings of the file, the second one made independently. */
	if ((fd2 = open(FILE1, O_RDWR)) < 0) e(5);
	q = mmap(NULL, 2 * pg, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);
	if (q == MAP_FAILED) e(6);
	if (q[10] != 'c') e(7);
	q[pg + 1] = 'q';
	if (p[pg + 1] != 'q') e(8);
	p[pg + 2] = 'p';
	if (q[pg + 2] != 'p') e(9);

	/* A private mapping is a copy once written to. */
	{
		char *r = mmap(NULL, pg, PROT_READ | PROT_WRITE, MAP_PRIVATE,
		    fd2, 0);
		if (r == MAP_FAILED) e(10);
		r[10] = 'r';
		if (p[10] != 'c' || byte_at(fd, 10) != 'c') e(11);
		munmap(r, pg);
	}

	munmap(q, 2 * pg);
	close(fd2);
	munmap(p, 2 * pg);
	close(fd);
}

static void
test_holes_and_end(void)
{
	char *p, c;
	int fd;

	subtest = 3;
	/* A file of three pages, all holes. */
	if ((fd = open(FILE2, O_RDWR | O_CREAT | O_TRUNC, 0644)) < 0) e(1);
	if (ftruncate(fd, 3 * pg) != 0) e(2);
	p = mmap(NULL, 4 * pg, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) e(3);

	/* Writing to a hole fills it in. */
	p[5] = 'h';
	if (byte_at(fd, 5) != 'h') e(4);

	/* A hole read through the mapping, then filled by write(2). */
	if (p[pg + 5] != 0) e(5);
	c = 'w';
	if (pwrite(fd, &c, 1, pg + 5) != 1) e(6);
	if (p[pg + 5] != 'w') e(7);
	/* ...and then written to through the mapping. */
	p[pg + 6] = 'm';
	if (byte_at(fd, pg + 6) != 'm') e(8);

	/* Beyond the end of the file: SIGBUS, for reads and writes. */
	if (touch(p + 3 * pg, 0) != SIGBUS) e(9);
	if (touch(p + 3 * pg + 1, 1) != SIGBUS) e(10);
	/* The size does not change by writing through a mapping. */
	{
		struct stat st;
		if (fstat(fd, &st) != 0 || st.st_size != 3 * pg) e(11);
	}

	/* Truncation takes the pages away. */
	if (p[2 * pg] != 0) e(12);
	p[2 * pg] = 't';
	if (ftruncate(fd, 2 * pg) != 0) e(13);
	if (touch(p + 2 * pg, 0) != SIGBUS) e(14);
	if (ftruncate(fd, 3 * pg) != 0) e(15);
	if (p[2 * pg] != 0) e(16);		/* a new hole, not 't' */

	munmap(p, 4 * pg);
	close(fd);
}

static void
test_permissions(void)
{
	char *p;
	int fd;

	subtest = 4;
	fd = make_file(FILE1, 1, 'a');
	close(fd);

	/* Writing to a file needs it open for writing. */
	if ((fd = open(FILE1, O_RDONLY)) < 0) e(1);
	if (mmap(NULL, pg, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) !=
	    MAP_FAILED || errno != EACCES) e(2);
	p = mmap(NULL, pg, PROT_READ, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) e(3);
	if (mprotect(p, pg, PROT_READ | PROT_WRITE) != -1 || errno != EACCES)
		e(4);
	if (touch(p, 1) != SIGSEGV) e(5);
	munmap(p, pg);
	/* Privately, anything goes. */
	p = mmap(NULL, pg, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (p == MAP_FAILED) e(6);
	p[0] = 'z';
	if (byte_at(fd, 0) != 'a') e(7);
	munmap(p, pg);
	close(fd);

	/* Read-only first, writable later: fine if the file allows it. */
	if ((fd = open(FILE1, O_RDWR)) < 0) e(8);
	p = mmap(NULL, pg, PROT_READ, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) e(9);
	if (touch(p, 1) != SIGSEGV) e(10);
	if (mprotect(p, pg, PROT_READ | PROT_WRITE) != 0) e(11);
	p[0] = 'y';
	if (byte_at(fd, 0) != 'y') e(12);
	/* And read-only again. */
	if (mprotect(p, pg, PROT_READ) != 0) e(13);
	if (touch(p + 1, 1) != SIGSEGV) e(14);
	if (byte_at(fd, 1) != 'a') e(15);

	/* msync arguments. */
	if (msync(p, pg, MS_SYNC | MS_ASYNC) != -1 || errno != EINVAL) e(16);
	if (msync(p + 1, pg, MS_SYNC) != -1 || errno != EINVAL) e(17);
	if (msync(p, pg, MS_INVALIDATE) != 0) e(18);
	munmap(p, pg);
	if (msync(p, pg, MS_SYNC) != -1 || errno != ENOMEM) e(19);
	close(fd);
}

/* Empty the caches of the file's file system (needs root; the test runs as
 * root), so that what is mapped next comes in through VFS.
 */
static void
flush_cache(int fd)
{

	if (geteuid() == 0 && fcntl(fd, F_FLUSH_FS_CACHE) != 0) e(95);
}

static void
on_alarm(int sig)
{

	printf("subtest %d: hung\n", subtest);
	fflush(stdout);
	_exit(1);
}

static void
test_io_into_mapping(void)
{
	char *p, buf[64];
	int fd;

	subtest = 5;
	/* read(2) and write(2) of a file to and from a shared mapping of
	 * itself, with pages that are not in yet: the file is busy with the
	 * call while the pages are brought in.
	 */
	signal(SIGALRM, on_alarm);
	alarm(30);
	fd = make_file(FILE1, 4, 'a');
	if (pwrite(fd, "0123456789", 10, 3 * pg) != 10) e(1);
	flush_cache(fd);		/* so that pages come in from VFS */
	p = mmap(NULL, 4 * pg, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) e(2);
	if (pread(fd, p + 10, 10, 3 * pg) != 10) e(3);
	if (memcmp(p + 10, "0123456789", 10) != 0) e(4);
	if (byte_at(fd, 15) != '5') e(5);
	if (pwrite(fd, p + 2 * pg, 64, 0) != 64) e(6);
	munmap(p, 4 * pg);

	/* The same with a private mapping. */
	flush_cache(fd);
	p = mmap(NULL, 4 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (p == MAP_FAILED) e(7);
	if (pwrite(fd, p + pg, 64, 2 * pg) != 64) e(8);
	if (pread(fd, p + 2 * pg + 100, 10, 3 * pg) != 10) e(9);
	munmap(p, 4 * pg);
	alarm(0);

	/* Page-ins do not move the file position. */
	if (lseek(fd, 7, SEEK_SET) != 7) e(10);
	flush_cache(fd);
	p = mmap(NULL, 4 * pg, PROT_READ, MAP_PRIVATE, fd, 0);
	if (p == MAP_FAILED) e(11);
	if (p[3 * pg] != '0' || p[2 * pg] != 'a') e(12);
	if (lseek(fd, 0, SEEK_CUR) != 7) e(13);
	if (read(fd, buf, 3) != 3 || memcmp(buf, "aaa", 3) != 0) e(14);
	munmap(p, 4 * pg);
	p = mmap(NULL, 4 * pg, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) e(15);
	p[pg] = 'k';
	if (lseek(fd, 0, SEEK_CUR) != 10) e(16);

	/* A shared mapping stays the file when the cache is emptied. */
	flush_cache(fd);
	if (pwrite(fd, "R", 1, pg) != 1) e(17);
	if (p[pg] != 'R') e(18);
	p[pg + 1] = 'S';
	if (byte_at(fd, pg + 1) != 'S') e(19);
	munmap(p, 4 * pg);
	close(fd);
}

static void
test_large_offset(void)
{
	off_t big = (off_t)5 << 30;		/* 5 GiB: past 32 bits */
	char *p;
	int fd;

	subtest = 6;
	if ((fd = open(FILE2, O_RDWR | O_CREAT | O_TRUNC, 0644)) < 0) e(1);
	if (ftruncate(fd, big + pg) != 0) {
		/* The file system may not do files this large. */
		close(fd);
		return;
	}
	p = mmap(NULL, pg, PROT_READ | PROT_WRITE, MAP_SHARED, fd, big);
	if (p == MAP_FAILED) e(2);
	if (p[0] != 0) e(3);
	p[1] = 'L';
	if (byte_at(fd, big + 1) != 'L') e(4);
	munmap(p, pg);
	if (ftruncate(fd, 0) != 0) e(5);
	close(fd);
}

#define RAMDISK	"/dev/ram5"
#define MNT	"t128mnt"
#define SILENT	" >/dev/null 2>&1"

static void
test_persistence(void)
{
	char *p;
	int fd;

	subtest = 7;
	/* What the mapping wrote reaches the disk, including what it wrote
	 * after the blocks were written out once: unmount (which drops the
	 * cache) and mount again.  Needs root, for a file system of its own.
	 */
	if (geteuid() != 0)
		return;
	if (system("ramdisk 2048 " RAMDISK SILENT) != 0 ||
	    system("mkfs.mfs " RAMDISK SILENT) != 0 ||
	    (mkdir(MNT, 0755) != 0 && errno != EEXIST) ||
	    system("mount " RAMDISK " " MNT SILENT) != 0) {
		printf("subtest 7 skipped: no ramdisk file system\n");
		return;
	}

	fd = make_file(MNT "/f", 4, 'a');
	p = mmap(NULL, 4 * pg, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) e(1);
	p[1] = 'b';			/* then written out by sync() */
	p[pg + 1] = 'c';
	sync();
	p[2] = 'd';			/* after: must make it dirty again */
	p[pg + 2] = 'e';
	p[3 * pg] = 'f';		/* first write to this page */
	if (munmap(p, 4 * pg) != 0) e(2);
	close(fd);

	if (system("umount " RAMDISK SILENT) != 0) e(3);
	if (system("mount " RAMDISK " " MNT SILENT) != 0) e(4);

	if ((fd = open(MNT "/f", O_RDONLY)) < 0) e(5);
	if (byte_at(fd, 1) != 'b' || byte_at(fd, pg + 1) != 'c') e(6);
	if (byte_at(fd, 2) != 'd' || byte_at(fd, pg + 2) != 'e') e(7);
	if (byte_at(fd, 3 * pg) != 'f' || byte_at(fd, 3 * pg + 1) != 'a') e(8);
	close(fd);

	(void)system("umount " RAMDISK SILENT);
	(void)rmdir(MNT);
}

int
main(int argc, char **argv)
{
	struct sigaction sa;

	start(128);
	pg = sysconf(_SC_PAGESIZE);

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_bus;
	if (sigaction(SIGBUS, &sa, NULL) != 0) e(1);
	if (sigaction(SIGSEGV, &sa, NULL) != 0) e(2);

	test_coherence();
	test_sharing();
	test_holes_and_end();
	test_permissions();
	test_io_into_mapping();
	test_large_offset();
	test_persistence();

	(void)unlink(FILE1);
	(void)unlink(FILE2);
	quit();
	return 0;
}
