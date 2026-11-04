/* Test 167 - tmpfs: an in-memory file system, and mmap(2) of its files.
 *
 * Mounts a tmpfs (so it runs as root), then checks files, directories and
 * links on it, that read(2), write(2) and ftruncate(2) agree with MAP_SHARED
 * mappings of a file, in this process and across fork(2), that a file lives
 * on while open after unlink(2), that the size= limit holds, and that a
 * program on tmpfs runs.
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

#define MNT	"mnt"

static long pg;

static void
do_mount(const char *opts)
{
	char cmd[256];

	snprintf(cmd, sizeof(cmd), "mount -t tmpfs %s%s none %s >/dev/null",
	    opts ? "-o " : "", opts ? opts : "", MNT);
	if (system(cmd) != 0) e(100);
}

static void
do_umount(void)
{

	if (system("umount " MNT " >/dev/null") != 0) e(101);
}

/* Files, directories and links. */
static void
test167a(void)
{
	struct stat st;
	struct statvfs sv;
	DIR *dp;
	struct dirent *de;
	char buf[64];
	int fd, n;

	subtest = 1;

	if ((fd = open(MNT "/f", O_RDWR | O_CREAT | O_EXCL, 0640)) < 0) e(1);
	if (write(fd, "hello", 5) != 5) e(2);
	if (lseek(fd, 3 * pg, SEEK_SET) != 3 * pg) e(3);
	if (write(fd, "x", 1) != 1) e(4);
	if (fstat(fd, &st) != 0) e(5);
	if (st.st_size != 3 * pg + 1 || !S_ISREG(st.st_mode) ||
	    (st.st_mode & 0777) != 0640 || st.st_nlink != 1) e(6);
	if (st.st_blocks != 2 * pg / 512) e(7);	/* a hole between */
	if (pread(fd, buf, 5, 0) != 5 || memcmp(buf, "hello", 5)) e(8);
	if (pread(fd, buf, 4, pg) != 4 || memcmp(buf, "\0\0\0\0", 4)) e(9);
	if (ftruncate(fd, 2) != 0) e(10);
	if (ftruncate(fd, 10) != 0) e(11);
	if (pread(fd, buf, 10, 0) != 10 || memcmp(buf, "he\0\0\0\0\0\0\0\0", 10))
		e(12);
	close(fd);

	if (mkdir(MNT "/d", 0755) != 0) e(13);
	if (mkdir(MNT "/d", 0755) != -1 || errno != EEXIST) e(14);
	if (link(MNT "/f", MNT "/d/g") != 0) e(15);
	if (stat(MNT "/f", &st) != 0 || st.st_nlink != 2) e(16);
	if (symlink("../f", MNT "/d/s") != 0) e(17);
	if ((n = readlink(MNT "/d/s", buf, sizeof(buf))) != 4 ||
	    memcmp(buf, "../f", 4)) e(18);
	if (stat(MNT "/d/s", &st) != 0 || st.st_size != 10) e(19);
	if (rmdir(MNT "/d") != -1 || errno != ENOTEMPTY) e(20);
	if (unlink(MNT "/d") != -1 || errno != EPERM) e(21);
	if (rename(MNT "/d/g", MNT "/h") != 0) e(22);
	if (rename(MNT "/d", MNT "/d/x") != -1 || errno != EINVAL) e(23);
	if (stat(MNT "/d", &st) != 0 || st.st_nlink != 2) e(24);
	if (stat(MNT, &st) != 0 || st.st_nlink != 3 ||
	    (st.st_mode & 07777) != 01777) e(25);

	if ((dp = opendir(MNT "/d")) == NULL) e(26);
	n = 0;
	while ((de = readdir(dp)) != NULL)
		if (!strcmp(de->d_name, "s") && de->d_type == DT_LNK)
			n++;
		else if (strcmp(de->d_name, ".") && strcmp(de->d_name, ".."))
			e(27);
	if (n != 1) e(28);
	closedir(dp);

	if (unlink(MNT "/d/s") != 0) e(29);
	if (rmdir(MNT "/d") != 0) e(30);
	if (unlink(MNT "/h") != 0) e(31);
	if (unlink(MNT "/f") != 0) e(32);
	if (stat(MNT, &st) != 0 || st.st_nlink != 2) e(33);

	if (statvfs(MNT, &sv) != 0 || sv.f_bsize != (unsigned long)pg ||
	    sv.f_bfree != sv.f_blocks) e(34);
}

/* read(2), write(2), ftruncate(2) and MAP_SHARED mappings agree. */
static void
test167b(void)
{
	char *p, *q, buf[16];
	pid_t pid;
	int fd, status;

	subtest = 2;

	if ((fd = open(MNT "/m", O_RDWR | O_CREAT | O_TRUNC, 0600)) < 0) e(1);
	if (ftruncate(fd, 2 * pg) != 0) e(2);
	p = mmap(NULL, 2 * pg, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) e(3);
	if (p[0] != 0 || p[2 * pg - 1] != 0) e(4);

	/* Through the mapping, then with read(2). */
	memcpy(p + pg - 2, "abcd", 4);
	if (pread(fd, buf, 4, pg - 2) != 4 || memcmp(buf, "abcd", 4)) e(5);

	/* With write(2), then through the mapping. */
	if (pwrite(fd, "WXYZ", 4, 100) != 4) e(6);
	if (memcmp(p + 100, "WXYZ", 4)) e(7);

	/* Another mapping of the same file sees the same pages. */
	q = mmap(NULL, pg, PROT_READ, MAP_SHARED, fd, pg);
	if (q == MAP_FAILED) e(8);
	if (memcmp(q, "cd", 2)) e(9);
	if (munmap(q, pg) != 0) e(9);

	/* A child writes; the parent sees it, in memory and in the file. */
	if ((pid = fork()) == -1) e(10);
	if (pid == 0) {
		memcpy(p + 200, "child", 5);
		_exit(0);
	}
	if (waitpid(pid, &status, 0) != pid || status != 0) e(11);
	if (memcmp(p + 200, "child", 5)) e(12);
	if (pread(fd, buf, 5, 200) != 5 || memcmp(buf, "child", 5)) e(13);

	/* A private mapping copies on write and leaves the file alone. */
	q = mmap(NULL, pg, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (q == MAP_FAILED) e(14);
	if (memcmp(q + 100, "WXYZ", 4)) e(15);
	q[100] = 'P';
	if (p[100] != 'W') e(16);
	if (munmap(q, pg) != 0) e(17);

	/* Shrink: what lay past the new end reads as zeroes on regrowth,
	 * even what was written there through the mapping. */
	if (ftruncate(fd, 150) != 0) e(18);
	p[160] = 'Z';			/* past the end, same page */
	if (ftruncate(fd, 2 * pg) != 0) e(19);
	if (pread(fd, buf, 1, 160) != 1 || buf[0] != 0) e(20);
	if (pread(fd, buf, 4, 100) != 4 || memcmp(buf, "WXYZ", 4)) e(21);

	/* Unlinked while open and mapped: still there. */
	if (unlink(MNT "/m") != 0) e(22);
	p[0] = 'U';
	if (pread(fd, buf, 1, 0) != 1 || buf[0] != 'U') e(23);
	if (munmap(p, 2 * pg) != 0) e(24);
	p = mmap(NULL, pg, PROT_READ, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED || p[0] != 'U') e(25);
	if (munmap(p, pg) != 0) e(26);
	close(fd);
}

/* The size= limit, and a program on tmpfs. */
static void
test167c(void)
{
	struct statvfs sv;
	char buf[4096];
	int fd, i;
	ssize_t r;

	subtest = 3;

	do_umount();
	do_mount("size=64k");
	if (statvfs(MNT, &sv) != 0 || sv.f_blocks * sv.f_frsize != 65536)
		e(1);
	if ((fd = open(MNT "/big", O_WRONLY | O_CREAT, 0600)) < 0) e(2);
	memset(buf, 'b', sizeof(buf));
	for (i = 0; i < 32; i++)
		if ((r = write(fd, buf, sizeof(buf))) != sizeof(buf))
			break;
	if (i != 16 || r != -1 || errno != ENOSPC) e(3);
	close(fd);
	if (unlink(MNT "/big") != 0) e(4);
	if (statvfs(MNT, &sv) != 0 || sv.f_bfree != sv.f_blocks) e(5);

	do_umount();
	do_mount(NULL);
	if (system("cp /bin/echo " MNT "/echo && " MNT "/echo ok >" MNT
	    "/out") != 0) e(6);
	if ((fd = open(MNT "/out", O_RDONLY)) < 0) e(7);
	if (read(fd, buf, sizeof(buf)) != 3 || memcmp(buf, "ok\n", 3)) e(8);
	close(fd);
}

int
main(int argc, char **argv)
{

	start(167);
	pg = sysconf(_SC_PAGESIZE);

	/* We run setuid root, but the shell that system() starts for mount(8)
	 * would drop back to the real user. */
	if (setuid(0) != 0) e(0);

	if (mkdir(MNT, 0755) != 0) e(0);
	do_mount(NULL);

	test167a();
	test167b();
	test167c();

	do_umount();
	quit();
	return 0;
}
