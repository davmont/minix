/* Test 107 - POSIX record locks (fcntl F_GETLK, F_SETLK, F_SETLKW).
 *
 * A lock belongs to a process, in all of its threads.  A process's own locks
 * never conflict: a new lock replaces what it held over the range, and its
 * locks of one type that touch are merged.  Unlocking touches only the
 * caller's locks.  Closing any descriptor of a file releases them all.
 * Conflicts are checked from a child process.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

#define FILE1	"lockfile"

static int fd;		/* the parent's descriptor of FILE1 */

static int
setlk(int lfd, int cmd, int type, off_t start, off_t len)
{
	struct flock fl;

	memset(&fl, 0, sizeof(fl));
	fl.l_type = type;
	fl.l_whence = SEEK_SET;
	fl.l_start = start;
	fl.l_len = len;
	return fcntl(lfd, cmd, &fl);
}

/* Can another process set this lock now?  1 yes, 0 no (EAGAIN), -1 error. */
static int
other_can_lock(int type, off_t start, off_t len)
{
	int status, cfd, r;
	pid_t pid;

	if ((pid = fork()) < 0) return -1;
	if (pid == 0) {
		if ((cfd = open(FILE1, O_RDWR)) < 0) _exit(10);
		r = setlk(cfd, F_SETLK, type, start, len);
		if (r == 0) _exit(1);
		_exit(errno == EAGAIN ? 0 : 11);
	}
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status)) return -1;
	switch (WEXITSTATUS(status)) {
	case 1: return 1;
	case 0: return 0;
	default: return -1;
	}
}

/* What does another process's F_GETLK report for this range? */
static int
other_getlk(int type, off_t start, off_t len, struct flock *res)
{
	int p[2], status, cfd;
	pid_t pid;
	struct flock fl;

	if (pipe(p) != 0) return -1;
	if ((pid = fork()) < 0) return -1;
	if (pid == 0) {
		close(p[0]);
		if ((cfd = open(FILE1, O_RDWR)) < 0) _exit(10);
		memset(&fl, 0, sizeof(fl));
		fl.l_type = type;
		fl.l_whence = SEEK_SET;
		fl.l_start = start;
		fl.l_len = len;
		if (fcntl(cfd, F_GETLK, &fl) != 0) _exit(11);
		if (write(p[1], &fl, sizeof(fl)) != sizeof(fl)) _exit(12);
		_exit(0);
	}
	close(p[1]);
	if (read(p[0], res, sizeof(*res)) != sizeof(*res)) return -1;
	close(p[0]);
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0) return -1;
	return 0;
}

static void
unlock_all(void)
{
	if (setlk(fd, F_SETLK, F_UNLCK, 0, 0) != 0) e(90);
}

static void
test_basic(void)
{
	struct flock fl;

	subtest = 1;
	if (setlk(fd, F_SETLK, F_WRLCK, 0, 10) != 0) e(1);
	if (other_can_lock(F_WRLCK, 5, 10) != 0) e(2);
	if (other_can_lock(F_RDLCK, 9, 1) != 0) e(3);
	if (other_can_lock(F_WRLCK, 10, 10) != 1) e(4);

	/* F_GETLK names the lock and the process (not a thread) holding it. */
	if (other_getlk(F_WRLCK, 5, 10, &fl) != 0) e(5);
	if (fl.l_type != F_WRLCK || fl.l_start != 0 || fl.l_len != 10 ||
	    fl.l_pid != getpid()) e(6);
	if (other_getlk(F_WRLCK, 20, 5, &fl) != 0) e(7);
	if (fl.l_type != F_UNLCK) e(8);
	unlock_all();
	if (other_can_lock(F_WRLCK, 0, 0) != 1) e(9);
}

/* Another process's unlock must not release our lock. */
static void
test_unlock_other(void)
{
	int status, cfd;
	pid_t pid;

	subtest = 2;
	if (setlk(fd, F_SETLK, F_WRLCK, 0, 100) != 0) e(1);
	if ((pid = fork()) < 0) e(2);
	if (pid == 0) {
		if ((cfd = open(FILE1, O_RDWR)) < 0) _exit(10);
		if (setlk(cfd, F_SETLK, F_UNLCK, 0, 0) != 0) _exit(11);
		/* Still locked by the parent. */
		if (setlk(cfd, F_SETLK, F_WRLCK, 0, 100) != -1 ||
		    errno != EAGAIN) _exit(12);
		_exit(0);
	}
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0) e(3);
	if (other_can_lock(F_RDLCK, 50, 1) != 0) e(4);
	unlock_all();
}

/* Our own locks: upgrade, downgrade, split, merge. */
static void
test_own(void)
{
	struct flock fl;

	subtest = 3;
	if (setlk(fd, F_SETLK, F_RDLCK, 0, 100) != 0) e(1);
	if (setlk(fd, F_SETLK, F_WRLCK, 10, 10) != 0) e(2);	/* upgrade */
	if (other_can_lock(F_RDLCK, 0, 10) != 1) e(3);
	if (other_can_lock(F_RDLCK, 10, 1) != 0) e(4);
	if (other_can_lock(F_RDLCK, 20, 80) != 1) e(5);
	if (setlk(fd, F_SETLK, F_RDLCK, 10, 10) != 0) e(6);	/* downgrade */
	if (other_can_lock(F_RDLCK, 0, 100) != 1) e(7);
	if (other_can_lock(F_WRLCK, 15, 1) != 0) e(8);
	unlock_all();

	/* Unlocking the middle splits a lock... */
	if (setlk(fd, F_SETLK, F_WRLCK, 0, 100) != 0) e(9);
	if (setlk(fd, F_SETLK, F_UNLCK, 40, 20) != 0) e(10);
	if (other_can_lock(F_WRLCK, 45, 5) != 1) e(11);
	if (other_can_lock(F_WRLCK, 30, 5) != 0) e(12);
	if (other_can_lock(F_WRLCK, 60, 5) != 0) e(13);
	/* ... and locking it again merges the pieces into one lock. */
	if (setlk(fd, F_SETLK, F_WRLCK, 40, 20) != 0) e(14);
	if (other_getlk(F_WRLCK, 50, 1, &fl) != 0) e(15);
	if (fl.l_type != F_WRLCK || fl.l_start != 0 || fl.l_len != 100) e(16);
	unlock_all();
	if (other_can_lock(F_WRLCK, 0, 0) != 1) e(17);
}

/* Many locks: the table is not a handful of slots. */
static void
test_many(void)
{
	int i;

	subtest = 4;
	for (i = 0; i < 200; i++) {
		if (setlk(fd, F_SETLK, F_WRLCK, 2 * i, 1) != 0) {
			e(1);
			break;
		}
	}
	if (other_can_lock(F_WRLCK, 2 * 150, 1) != 0) e(2);
	if (other_can_lock(F_WRLCK, 2 * 150 + 1, 1) != 1) e(3);
	unlock_all();
	if (other_can_lock(F_WRLCK, 0, 0) != 1) e(4);
}

/* Closing any descriptor of the file releases the process's locks. */
static void
test_close(void)
{
	int fd2;

	subtest = 5;
	if (setlk(fd, F_SETLK, F_WRLCK, 0, 10) != 0) e(1);
	if ((fd2 = open(FILE1, O_RDONLY)) < 0) e(2);
	if (other_can_lock(F_WRLCK, 0, 10) != 0) e(3);
	close(fd2);
	if (other_can_lock(F_WRLCK, 0, 10) != 1) e(4);
}

/* A negative length covers the bytes before l_start. */
static void
test_negative_len(void)
{
	subtest = 6;
	if (setlk(fd, F_SETLK, F_WRLCK, 10, -5) != 0) e(1);	/* bytes 5-9 */
	if (other_can_lock(F_WRLCK, 4, 1) != 1) e(2);
	if (other_can_lock(F_WRLCK, 5, 1) != 0) e(3);
	if (other_can_lock(F_WRLCK, 9, 1) != 0) e(4);
	if (other_can_lock(F_WRLCK, 10, 1) != 1) e(5);
	if (setlk(fd, F_SETLK, F_WRLCK, 3, -5) != -1 || errno != EINVAL)
		e(6);
	unlock_all();
}

/* F_SETLKW waits for the conflicting lock to go away. */
static void
test_wait(void)
{
	int p[2], status, cfd;
	char c;
	pid_t pid;

	subtest = 7;
	if (setlk(fd, F_SETLK, F_WRLCK, 0, 10) != 0) e(1);
	if (pipe(p) != 0) e(2);
	if ((pid = fork()) < 0) e(3);
	if (pid == 0) {
		close(p[0]);
		if ((cfd = open(FILE1, O_RDWR)) < 0) _exit(10);
		alarm(20);
		if (setlk(cfd, F_SETLKW, F_WRLCK, 5, 1) != 0) _exit(11);
		c = 'x';
		(void)write(p[1], &c, 1);
		_exit(0);
	}
	close(p[1]);
	usleep(300000);		/* the child is waiting by now */
	unlock_all();
	alarm(20);
	if (read(p[0], &c, 1) != 1) e(4);
	alarm(0);
	close(p[0]);
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0) e(5);
}

static int thread_result;
static int thread_fd;

static void *
thread_main(void *arg)
{
	/* The process's lock, set by the main thread: no conflict here. */
	if (setlk(fd, F_SETLK, F_WRLCK, 0, 10) != 0) thread_result = 1;
	else if (setlk(fd, F_SETLK, F_RDLCK, 0, 20) != 0) thread_result = 2;
	/* Closing a descriptor of the file, in this thread, releases the
	 * locks of the whole process. */
	else if (close(thread_fd) != 0) thread_result = 3;
	return arg;
}

/* Locks belong to the process, not to the thread that set them. */
static void
test_threads(void)
{
	pthread_t t;

	subtest = 8;
	if ((thread_fd = open(FILE1, O_RDONLY)) < 0) e(1);
	if (setlk(fd, F_SETLK, F_WRLCK, 0, 10) != 0) e(2);
	thread_result = 0;
	if (pthread_create(&t, NULL, thread_main, NULL) != 0) e(3);
	if (pthread_join(t, NULL) != 0) e(4);
	if (thread_result != 0) e(10 + thread_result);
	if (other_can_lock(F_WRLCK, 0, 20) != 1) e(5);
}

int
main(int argc, char **argv)
{
	start(107);

	if ((fd = open(FILE1, O_RDWR | O_CREAT, 0644)) < 0) e(1);
	if (write(fd, "0123456789", 10) != 10) e(2);

	test_basic();
	test_unlock_other();
	test_own();
	test_many();
	test_close();
	test_negative_len();
	test_wait();
	test_threads();

	close(fd);
	quit();
	return 0;
}
