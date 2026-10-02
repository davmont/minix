/* Test 112 - flock(2): whole-file locks that belong to the open file.
 *
 * Unlike fcntl(2) record locks, which belong to the process, a flock lock
 * belongs to the open file: two opens in one process conflict, dup(2) and
 * fork(2) share it, and only the last close of the open file releases it.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

#define FILE1	"flockfile"

static int
op(const char *name)
{
	int fd;

	if ((fd = open(name, O_RDWR)) < 0) e(90);
	return fd;
}

/* Can a fresh open of the file take this lock now (1), or not (0)? */
static int
can_flock(int how)
{
	int fd, r;

	fd = op(FILE1);
	r = flock(fd, how | LOCK_NB);
	if (r != 0 && errno != EWOULDBLOCK) e(91);
	close(fd);
	return r == 0;
}

static void
test_basic(void)
{
	int fd1, fd2;

	subtest = 1;
	fd1 = op(FILE1);
	fd2 = op(FILE1);
	/* Two open files of one process conflict (fcntl locks would not). */
	if (flock(fd1, LOCK_EX) != 0) e(1);
	if (flock(fd2, LOCK_EX | LOCK_NB) != -1 || errno != EWOULDBLOCK) e(2);
	if (flock(fd2, LOCK_SH | LOCK_NB) != -1 || errno != EWOULDBLOCK) e(3);
	if (flock(fd1, LOCK_UN) != 0) e(4);
	if (flock(fd2, LOCK_EX | LOCK_NB) != 0) e(5);
	if (flock(fd2, LOCK_UN) != 0) e(6);

	subtest = 2;
	/* Shared locks share; an exclusive one waits for them. */
	if (flock(fd1, LOCK_SH) != 0) e(1);
	if (flock(fd2, LOCK_SH | LOCK_NB) != 0) e(2);
	if (can_flock(LOCK_EX)) e(3);
	if (!can_flock(LOCK_SH)) e(4);
	if (flock(fd2, LOCK_UN) != 0) e(5);
	/* The only holder may convert its lock. */
	if (flock(fd1, LOCK_EX | LOCK_NB) != 0) e(6);
	if (can_flock(LOCK_SH)) e(7);
	if (flock(fd1, LOCK_SH) != 0) e(8);		/* and back */
	if (!can_flock(LOCK_SH)) e(9);
	close(fd1);
	close(fd2);
	if (!can_flock(LOCK_EX)) e(10);
}

static void
test_close(void)
{
	int fd1, fd3, fd4;

	subtest = 3;
	/* dup(2) shares the open file and its lock. */
	fd1 = op(FILE1);
	if (flock(fd1, LOCK_EX) != 0) e(1);
	if ((fd3 = dup(fd1)) < 0) e(2);
	close(fd3);				/* not the last close */
	if (can_flock(LOCK_EX)) e(3);

	subtest = 4;
	/* Closing another open file of the same file does not release it
	 * (it would release a process's fcntl locks). */
	fd4 = op(FILE1);
	close(fd4);
	if (can_flock(LOCK_EX)) e(1);
	close(fd1);				/* the last close */
	if (!can_flock(LOCK_EX)) e(2);
}

static void
test_fork(void)
{
	int fd1, status;
	pid_t pid;

	subtest = 5;
	/* A child shares the open file: its unlock releases the lock. */
	fd1 = op(FILE1);
	if (flock(fd1, LOCK_EX) != 0) e(1);
	if ((pid = fork()) < 0) e(2);
	if (pid == 0) {
		/* Already ours: no conflict. */
		if (flock(fd1, LOCK_EX | LOCK_NB) != 0) _exit(1);
		if (flock(fd1, LOCK_UN) != 0) _exit(2);
		_exit(0);
	}
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0) e(3);
	if (!can_flock(LOCK_EX)) e(4);
	close(fd1);
}

static void
test_wait(void)
{
	int fd1, p[2], status;
	char c;
	pid_t pid;

	subtest = 6;
	fd1 = op(FILE1);
	if (flock(fd1, LOCK_EX) != 0) e(1);
	if (pipe(p) != 0) e(2);
	if ((pid = fork()) < 0) e(3);
	if (pid == 0) {
		int fd;

		close(p[0]);
		fd = op(FILE1);
		alarm(20);
		if (flock(fd, LOCK_EX) != 0) _exit(1);	/* waits */
		c = 'x';
		(void)write(p[1], &c, 1);
		_exit(0);
	}
	close(p[1]);
	usleep(300000);				/* the child is waiting */
	if (flock(fd1, LOCK_UN) != 0) e(4);
	alarm(20);
	if (read(p[0], &c, 1) != 1) e(5);
	alarm(0);
	close(p[0]);
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0) e(6);
	close(fd1);
}

static void
test_fcntl(void)
{
	struct flock fl;
	int fd1, fd2, status;
	pid_t pid;

	subtest = 7;
	/* flock and fcntl locks conflict, as on BSD. */
	fd1 = op(FILE1);
	if (flock(fd1, LOCK_EX) != 0) e(1);
	if ((pid = fork()) < 0) e(2);
	if (pid == 0) {
		fd2 = op(FILE1);
		memset(&fl, 0, sizeof(fl));
		fl.l_type = F_WRLCK;
		fl.l_whence = SEEK_SET;
		if (fcntl(fd2, F_SETLK, &fl) != -1 || errno != EAGAIN) _exit(1);
		if (fcntl(fd2, F_GETLK, &fl) != 0) _exit(2);
		if (fl.l_type != F_WRLCK || fl.l_pid != -1) _exit(3);
		_exit(0);
	}
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0) e(3);
	close(fd1);

	subtest = 8;
	fd1 = op(FILE1);
	if (flock(fd1, 0) != -1 || errno != EINVAL) e(1);
	if (flock(fd1, LOCK_SH | LOCK_EX) != -1 || errno != EINVAL) e(2);
	if (flock(-1, LOCK_SH) != -1 || errno != EBADF) e(3);
	close(fd1);
	{
		int p[2];

		if (pipe(p) != 0) e(4);
		if (flock(p[0], LOCK_SH) != -1 || errno != EOPNOTSUPP) e(5);
		close(p[0]);
		close(p[1]);
	}
}

int
main(int argc, char **argv)
{
	int fd;

	start(112);

	if ((fd = open(FILE1, O_RDWR | O_CREAT, 0644)) < 0) e(1);
	close(fd);

	test_basic();
	test_close();
	test_fork();
	test_wait();
	test_fcntl();

	quit();
	return 0;
}
