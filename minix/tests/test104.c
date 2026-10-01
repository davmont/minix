/* Test 104 - the *at() system calls.
 *
 * Each call is checked against a directory other than the working one, with
 * short paths (which go inline in the message) and long ones, and for its
 * AT_* flags and dirfd errors.  The plain calls (open, stat, ...) are the
 * *at() calls with AT_FDCWD; the old per-call numbers must be gone.
 */
#include <sys/stat.h>
#include <sys/time.h>
#include <minix/callnr.h>
#include <lib.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

#define LONGNAME "a_name_longer_than_the_forty_bytes_a_message_holds_inline"

static int dfd, dfd2;

/* Does 'path' (relative to the working directory) exist, as type 'type'? */
static int
is(const char *path, mode_t type)
{
	struct stat st;

	return lstat(path, &st) == 0 && (st.st_mode & S_IFMT) == type;
}

static void
setup(void)
{
	if (mkdir("d", 0755) != 0) e(1);
	if (mkdir("d2", 0755) != 0) e(2);
	if ((dfd = open("d", O_RDONLY)) < 0) e(3);
	if ((dfd2 = open("d2", O_RDONLY)) < 0) e(4);
}

static void
test_open(void)
{
	int fd;
	char buf[4];

	subtest = 1;
	if ((fd = openat(dfd, "f", O_RDWR | O_CREAT | O_EXCL, 0644)) < 0) e(1);
	if (write(fd, "abc", 3) != 3) e(2);
	close(fd);
	if (!is("d/f", S_IFREG)) e(3);
	if (is("f", S_IFREG)) e(4);		/* not in the working dir */

	if ((fd = openat(dfd, "f", O_RDONLY)) < 0) e(5);
	if (read(fd, buf, sizeof(buf)) != 3 || memcmp(buf, "abc", 3)) e(6);
	close(fd);

	if ((fd = openat(dfd, LONGNAME, O_WRONLY | O_CREAT, 0644)) < 0) e(7);
	close(fd);
	if (!is("d/" LONGNAME, S_IFREG)) e(8);

	/* AT_FDCWD is the working directory. */
	if ((fd = openat(AT_FDCWD, "d/f", O_RDONLY)) < 0) e(9);
	close(fd);

	/* An absolute path ignores dirfd, even a bad one. */
	if ((fd = openat(-5, "/", O_RDONLY)) < 0) e(10);
	close(fd);

	/* A relative one needs a good one, of a directory. */
	if (openat(-5, "f", O_RDONLY) != -1 || errno != EBADF) e(11);
	if ((fd = openat(dfd, "f", O_RDONLY)) < 0) e(12);
	if (openat(fd, "f", O_RDONLY) != -1 || errno != ENOTDIR) e(13);
	close(fd);
	if (openat(dfd, "nonexistent", O_RDONLY) != -1 || errno != ENOENT)
		e(14);
}

static void
test_stat(void)
{
	struct stat st;

	subtest = 2;
	if (symlinkat("f", dfd, "l") != 0) e(1);
	if (!is("d/l", S_IFLNK)) e(2);
	if (fstatat(dfd, "l", &st, 0) != 0 || !S_ISREG(st.st_mode)) e(3);
	if (fstatat(dfd, "l", &st, AT_SYMLINK_NOFOLLOW) != 0 ||
	    !S_ISLNK(st.st_mode)) e(4);
	if (fstatat(dfd, LONGNAME, &st, 0) != 0 || !S_ISREG(st.st_mode)) e(5);
	if (fstatat(dfd, "l", &st, AT_REMOVEDIR) != -1 || errno != EINVAL)
		e(6);
	if (fstatat(dfd, ".", &st, 0) != 0 || !S_ISDIR(st.st_mode)) e(7);
}

static void
test_readlink(void)
{
	char buf[PATH_MAX];
	ssize_t n;

	subtest = 3;
	if ((n = readlinkat(dfd, "l", buf, sizeof(buf))) != 1 || buf[0] != 'f')
		e(1);
	if (readlinkat(dfd, "f", buf, sizeof(buf)) != -1 || errno != EINVAL)
		e(2);
}

static void
test_mkdir_unlink(void)
{
	subtest = 4;
	if (mkdirat(dfd, "sub", 0755) != 0) e(1);
	if (!is("d/sub", S_IFDIR)) e(2);
	if (mkfifoat(dfd, "fifo", 0644) != 0) e(3);
	if (!is("d/fifo", S_IFIFO)) e(4);
	if (mknodat(dfd, "fifo2", S_IFIFO | 0644, 0) != 0) e(5);
	if (!is("d/fifo2", S_IFIFO)) e(6);

	/* AT_REMOVEDIR picks rmdir. */
	if (unlinkat(dfd, "sub", AT_REMOVEDIR | AT_EACCESS) != -1 ||
	    errno != EINVAL) e(7);
	if (unlinkat(dfd, "fifo", AT_REMOVEDIR) != -1 || errno != ENOTDIR)
		e(8);
	if (unlinkat(dfd, "sub", 0) != -1) e(9);
	if (unlinkat(dfd, "sub", AT_REMOVEDIR) != 0) e(10);
	if (is("d/sub", S_IFDIR)) e(11);
	if (unlinkat(dfd, "fifo", 0) != 0 || unlinkat(dfd, "fifo2", 0) != 0)
		e(12);
	if (is("d/fifo", S_IFIFO)) e(13);
}

static void
test_link_rename(void)
{
	struct stat st;

	subtest = 5;
	/* Two directories, neither the working one. */
	if (linkat(dfd, "f", dfd2, "hard", 0) != 0) e(1);
	if (!is("d2/hard", S_IFREG)) e(2);
	if (renameat(dfd2, "hard", dfd, "moved") != 0) e(3);
	if (is("d2/hard", S_IFREG) || !is("d/moved", S_IFREG)) e(4);
	if (unlinkat(dfd, "moved", 0) != 0) e(5);

	/* linkat() links a symlink itself, unless AT_SYMLINK_FOLLOW. */
	if (linkat(dfd, "l", dfd2, "nofollow", 0) != 0) e(6);
	if (!is("d2/nofollow", S_IFLNK)) e(7);
	if (linkat(dfd, "l", dfd2, "follow", AT_SYMLINK_FOLLOW) != 0) e(8);
	if (!is("d2/follow", S_IFREG)) e(9);
	if (lstat("d/f", &st) != 0 || st.st_nlink != 2) e(10);
	if (linkat(dfd, "f", dfd2, "x", AT_SYMLINK_NOFOLLOW) != -1 ||
	    errno != EINVAL) e(11);
	if (unlinkat(dfd2, "follow", 0) != 0) e(12);
	if (unlinkat(dfd2, "nofollow", 0) != 0) e(13);

	/* The second dirfd is checked too. */
	if (renameat(dfd, "f", -5, "g") != -1 || errno != EBADF) e(14);
	if (!is("d/f", S_IFREG)) e(15);
}

static void
test_perm(void)
{
	struct stat st;
	int fd;

	subtest = 6;
	if (fchmodat(dfd, "f", 0600, 0) != 0) e(1);
	if (stat("d/f", &st) != 0 || (st.st_mode & 07777) != 0600) e(2);
	/* AT_SYMLINK_NOFOLLOW changes the link, not the file. */
	if (fchmodat(dfd, "l", 0640, AT_SYMLINK_NOFOLLOW) != 0) e(3);
	if (stat("d/f", &st) != 0 || (st.st_mode & 07777) != 0600) e(4);
	if (fchmodat(dfd, "f", 0600, AT_REMOVEDIR) != -1 || errno != EINVAL)
		e(5);

	if (fchownat(dfd, "f", 2, 3, 0) != 0) e(6);
	if (stat("d/f", &st) != 0 || st.st_uid != 2 || st.st_gid != 3) e(7);
	if (fchownat(dfd, "l", 4, 5, AT_SYMLINK_NOFOLLOW) != 0) e(8);
	if (stat("d/f", &st) != 0 || st.st_uid != 2 || st.st_gid != 3) e(9);
	if (lstat("d/l", &st) != 0 || st.st_uid != 4 || st.st_gid != 5) e(10);
	if (fchownat(dfd, "f", 0, 0, 0) != 0) e(11);

	/* faccessat() checks the real ids, or the effective ones with
	 * AT_EACCESS: on a 0600 root file, one set is root's and the other
	 * not.  The test runs setuid root, so the real uid may be root's or
	 * not; if it is, make the effective one not. */
	if (getuid() == 0) {
		if (seteuid(2) != 0) e(12);
		if (faccessat(dfd, "f", R_OK, 0) != 0) e(13);
		if (faccessat(dfd, "f", R_OK, AT_EACCESS) != -1 ||
		    errno != EACCES) e(14);
		if (seteuid(0) != 0) e(15);
	} else {
		if (faccessat(dfd, "f", R_OK, 0) != -1 || errno != EACCES)
			e(13);
		if (faccessat(dfd, "f", R_OK, AT_EACCESS) != 0) e(14);
	}
	if (faccessat(dfd, "f", R_OK, AT_SYMLINK_NOFOLLOW) != -1 ||
	    errno != EINVAL) e(17);

	if ((fd = openat(dfd, "f", O_RDONLY)) < 0) e(18);
	close(fd);
}

static void
test_utimens(void)
{
	struct timespec ts[2];
	struct stat st;

	subtest = 7;
	ts[0].tv_sec = 1000000; ts[0].tv_nsec = 0;
	ts[1].tv_sec = 2000000; ts[1].tv_nsec = 0;
	if (utimensat(dfd, "f", ts, 0) != 0) e(1);
	if (stat("d/f", &st) != 0 || st.st_mtime != 2000000 ||
	    st.st_atime != 1000000) e(2);
	if (utimensat(-5, "f", ts, 0) != -1 || errno != EBADF) e(3);
}

static void
test_dangling(void)
{
	int fd;

	subtest = 8;
	/* O_CREAT through a dangling relative symlink creates its target in
	 * the symlink's directory: the dirfd's, not the working one. */
	if (symlinkat("target", dfd, "dangling") != 0) e(1);
	if ((fd = openat(dfd, "dangling", O_WRONLY | O_CREAT, 0644)) < 0) e(2);
	close(fd);
	if (!is("d/target", S_IFREG)) e(3);
	if (is("target", S_IFREG)) e(4);

	/* And through a path, for a plain open. */
	if (symlink("target2", "d/dangling2") != 0) e(5);
	if ((fd = open("d/dangling2", O_WRONLY | O_CREAT, 0644)) < 0) e(6);
	close(fd);
	if (!is("d/target2", S_IFREG)) e(7);
	if (is("target2", S_IFREG)) e(8);
}

static void
test_plain(void)
{
	struct stat st;

	subtest = 10;
	/* lchown() and lchmod() change the link itself, not its target. */
	if (lchown("d/l", 6, 7) != 0) e(1);
	if (lstat("d/l", &st) != 0 || st.st_uid != 6 || st.st_gid != 7) e(2);
	if (stat("d/f", &st) != 0 || st.st_uid != 0) e(3);
	if (lchmod("d/l", 0600) != 0) e(4);
	if (lstat("d/l", &st) != 0 || (st.st_mode & 07777) != 0600) e(5);
	if (stat("d/f", &st) != 0 || (st.st_mode & 07777) != 0600) e(6);

	/* link() follows a symlink, as before (linkat() does not). */
	if (link("d/l", "d2/plain") != 0) e(7);
	if (!is("d2/plain", S_IFREG)) e(8);
	if (unlink("d2/plain") != 0) e(9);
}

static void
test_old_calls(void)
{
	/* The pre-*at() call numbers: open, creat, link, unlink, mkdir,
	 * mknod, chmod, chown, access, rename, rmdir, symlink, readlink, stat,
	 * lstat, utimens.  A binary still using them fails, with ENOSYS. */
	static const int old[] = { 3, 4, 6, 7, 9, 10, 11, 12, 15, 17, 18, 19,
	    20, 21, 23, 37 };
	message m;
	unsigned int i;

	subtest = 11;
	for (i = 0; i < sizeof(old) / sizeof(old[0]); i++) {
		memset(&m, 0, sizeof(m));
		if (_syscall(VFS_PROC_NR, VFS_BASE + old[i], &m) != -1 ||
		    errno != ENOSYS) e(i + 1);
	}
}

static void
test_moved_cwd(void)
{
	int fd;

	subtest = 9;
	/* The dirfd, not the working directory, whichever that is. */
	if (chdir("d2") != 0) e(1);
	if ((fd = openat(dfd, "f", O_RDONLY)) < 0) e(2);
	close(fd);
	if (openat(AT_FDCWD, "f", O_RDONLY) != -1 || errno != ENOENT) e(3);
	if (chdir("..") != 0) e(4);
}

int
main(int argc, char **argv)
{
	start(104);

	setup();
	test_open();
	test_stat();
	test_readlink();
	test_mkdir_unlink();
	test_link_rename();
	test_perm();
	test_utimens();
	test_dangling();
	test_moved_cwd();
	test_plain();
	test_old_calls();

	close(dfd);
	close(dfd2);
	quit();
	return 0;
}
