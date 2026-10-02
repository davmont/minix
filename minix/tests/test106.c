/* Test 106 - open(2) flags: O_NOFOLLOW, O_DIRECTORY, and FIFOs opened for
 * both reading and writing.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

static int
is_reg(const char *path)
{
	struct stat st;

	return lstat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static void
test_nofollow(void)
{
	int fd;

	subtest = 1;
	if ((fd = open("file", O_RDWR | O_CREAT, 0644)) < 0) e(1);
	close(fd);
	if (symlink("file", "link") != 0) e(2);
	if (symlink("missing", "dangling") != 0) e(3);
	if (mkdir("dir", 0755) != 0) e(4);
	if (symlink("../file", "dir/up") != 0) e(5);
	if (symlink("dir", "dirlink") != 0) e(6);

	/* A final symlink fails, whatever the access mode. */
	if (open("link", O_RDONLY | O_NOFOLLOW) != -1 || errno != ELOOP) e(7);
	if (open("link", O_WRONLY | O_NOFOLLOW) != -1 || errno != ELOOP) e(8);
	if (open("link", O_RDWR | O_NOFOLLOW) != -1 || errno != ELOOP) e(9);
	if (open("link", O_RDONLY | O_CREAT | O_NOFOLLOW, 0644) != -1 ||
	    errno != ELOOP) e(10);

	/* So does a dangling one, and O_CREAT does not create its target. */
	if (open("dangling", O_WRONLY | O_CREAT | O_NOFOLLOW, 0644) != -1 ||
	    errno != ELOOP) e(11);
	if (is_reg("missing")) e(12);
	/* ... which it does without O_NOFOLLOW (negative control). */
	if ((fd = open("dangling", O_WRONLY | O_CREAT, 0644)) < 0) e(13);
	close(fd);
	if (!is_reg("missing")) e(14);

	/* O_EXCL still gives EEXIST for an existing name. */
	if (open("link", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644) != -1
	    || errno != EEXIST) e(15);

	/* Not a symlink: opens as usual.  Symlinks before the last component
	 * are followed. */
	if ((fd = open("file", O_RDONLY | O_NOFOLLOW)) < 0) e(16);
	close(fd);
	if ((fd = open("dirlink/up", O_RDONLY)) < 0) e(17);
	close(fd);
	if (open("dir/up", O_RDONLY | O_NOFOLLOW) != -1 || errno != ELOOP)
		e(18);
	if ((fd = open("dirlink/../file", O_RDONLY | O_NOFOLLOW)) < 0) {
		/* "dirlink/.." is dir's parent: still the working directory */
		e(19);
	} else
		close(fd);

	/* Without O_NOFOLLOW the link is followed (negative control). */
	if ((fd = open("link", O_RDONLY)) < 0) e(20);
	close(fd);
}

static void
test_directory(void)
{
	int fd;

	subtest = 2;
	if ((fd = open("dir", O_RDONLY | O_DIRECTORY)) < 0) e(1);
	close(fd);
	if ((fd = open(".", O_RDONLY | O_DIRECTORY)) < 0) e(2);
	close(fd);
	/* A symlink to a directory is followed, unless O_NOFOLLOW. */
	if ((fd = open("dirlink", O_RDONLY | O_DIRECTORY)) < 0) e(3);
	close(fd);
	if (open("dirlink", O_RDONLY | O_DIRECTORY | O_NOFOLLOW) != -1 ||
	    errno != ELOOP) e(4);

	if (open("file", O_RDONLY | O_DIRECTORY) != -1 || errno != ENOTDIR)
		e(5);
	if (open("link", O_RDONLY | O_DIRECTORY) != -1 || errno != ENOTDIR)
		e(6);
	/* Without O_DIRECTORY a file opens (negative control). */
	if ((fd = open("file", O_RDONLY)) < 0) e(7);
	close(fd);

	/* O_CREAT cannot make a directory: EINVAL, and nothing created. */
	if (open("newdir", O_RDONLY | O_CREAT | O_DIRECTORY, 0755) != -1 ||
	    errno != EINVAL) e(8);
	if (access("newdir", F_OK) == 0) e(9);
	if (open("dir", O_RDONLY | O_CREAT | O_DIRECTORY, 0755) != -1 ||
	    errno != EINVAL) e(10);
}

static void
test_fifo_rdwr(void)
{
	char buf[8];
	int fd, status;
	pid_t pid;

	subtest = 3;
	if (mkfifo("fifo", 0644) != 0) e(1);

	/* Opening for both reading and writing never waits for a partner. */
	alarm(10);
	if ((fd = open("fifo", O_RDWR)) < 0) e(2);
	alarm(0);
	if (write(fd, "abc", 4) != 4) e(3);
	if (read(fd, buf, sizeof(buf)) != 4 || strcmp(buf, "abc")) e(4);
	close(fd);

	/* And it releases someone waiting for the other end. */
	if ((pid = fork()) < 0) e(5);
	if (pid == 0) {
		alarm(10);
		if ((fd = open("fifo", O_RDONLY)) < 0) _exit(1);
		if (read(fd, buf, sizeof(buf)) != 4 || strcmp(buf, "xyz"))
			_exit(2);
		_exit(0);
	}
	usleep(200000);			/* let the child block in open */
	alarm(10);
	if ((fd = open("fifo", O_RDWR)) < 0) e(6);
	if (write(fd, "xyz", 4) != 4) e(7);
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0) e(8);
	alarm(0);
	close(fd);
}

static void
test_fifo_flags(void)
{
	int rd_nb, wr, rd, fl;

	subtest = 4;
	/* Each opener of a FIFO keeps its own file flags. */
	if ((rd_nb = open("fifo", O_RDONLY | O_NONBLOCK)) < 0) e(1);
	if ((wr = open("fifo", O_WRONLY)) < 0) e(2);
	if ((rd = open("fifo", O_RDONLY)) < 0) e(3);

	if ((fl = fcntl(rd_nb, F_GETFL)) == -1 || !(fl & O_NONBLOCK)) e(4);
	if ((fl = fcntl(rd, F_GETFL)) == -1 || (fl & O_NONBLOCK)) e(5);
	if ((fl = fcntl(rd, F_GETFL)) == -1 || (fl & O_ACCMODE) != O_RDONLY)
		e(6);

	/* The non-blocking reader does not block on the empty FIFO. */
	alarm(10);
	if (read(rd_nb, &fl, 1) != -1 || errno != EAGAIN) e(7);
	alarm(0);

	close(rd_nb);
	close(wr);
	close(rd);
	unlink("fifo");
}

static void
test_cloexec_on_failure(void)
{
	DIR *dirp;
	int fd, fd2;

	subtest = 5;
	/* A failed open must not leave its O_CLOEXEC flag on the fd number it
	 * took; the next user of that number would inherit it.  opendir(3)
	 * opens with O_DIRECTORY | O_CLOEXEC, so it fails on a file only
	 * after taking the fd. */
	if ((dirp = opendir("file")) != NULL || errno != ENOTDIR) e(1);
	if ((fd = fcntl(0, F_DUPFD, 0)) < 0) e(2);
	if (fcntl(fd, F_GETFD) != 0) e(3);
	close(fd);

	if (open("file", O_RDONLY | O_DIRECTORY | O_CLOEXEC) != -1 ||
	    errno != ENOTDIR) e(4);
	if ((fd = dup(0)) < 0) e(5);
	if (fcntl(fd, F_GETFD) != 0) e(6);
	if ((fd2 = fcntl(0, F_DUPFD, 0)) < 0) e(7);
	if (fcntl(fd2, F_GETFD) != 0) e(8);
	close(fd);
	close(fd2);
}

static void
alarm_handler(int sig)
{
}

static void
test_fifo_open_interrupted(void)
{
	struct sigaction sa;
	int low, fd;

	subtest = 6;
	if (mkfifo("fifo2", 0644) != 0) e(1);

	/* The lowest free fd, which the blocking open takes. */
	if ((low = dup(0)) < 0) e(2);
	close(low);

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = alarm_handler;	/* no SA_RESTART */
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGALRM, &sa, NULL) != 0) e(3);
	alarm(1);
	if (open("fifo2", O_RDONLY) != -1 || errno != EINTR) e(4);
	alarm(0);

	/* The interrupted open gave its fd back... */
	if ((fd = dup(0)) != low) e(5);
	close(fd);
	/* ... and is no reader of the FIFO. */
	if (open("fifo2", O_WRONLY | O_NONBLOCK) != -1 || errno != ENXIO)
		e(6);

	sa.sa_handler = SIG_DFL;
	(void)sigaction(SIGALRM, &sa, NULL);
	unlink("fifo2");
}

int
main(int argc, char **argv)
{
	start(106);

	test_nofollow();
	test_directory();
	test_fifo_rdwr();
	test_fifo_flags();
	test_cloexec_on_failure();
	test_fifo_open_interrupted();

	quit();
	return 0;
}
