/* Test 147 - POSIX semaphores between processes.
 *
 * Named semaphores (sem_open, sem_close, sem_unlink) and process-shared
 * unnamed ones (sem_init with pshared, in shared memory) are kept by the IPC
 * server.  A process gets the same sem_t for the same name until it closes
 * it; the name follows the open(2) rules for O_CREAT and O_EXCL; a semaphore
 * lives on after sem_unlink() for whoever has it open, and the name is then
 * free for a new one.  sem_wait() blocks until a sem_post() from another
 * process, sem_timedwait() gives up at its timeout, sem_trywait() does not
 * block, and a caught signal interrupts a sem_wait() (EINTR).
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <semaphore.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

#define NAME1	"/t147_a"
#define NAME2	"/t147_b"

static void
catcher(int signo)
{
	(void)signo;
}

static void
deadline(struct timespec *ts, long ms)
{
	clock_gettime(CLOCK_REALTIME, ts);
	ts->tv_sec += ms / 1000;
	ts->tv_nsec += (ms % 1000) * 1000000L;
	if (ts->tv_nsec >= 1000000000L) {
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000L;
	}
}

static int
value(sem_t *s)
{
	int v = -1;

	if (sem_getvalue(s, &v) != 0)
		return -1;
	return v;
}

/* Names: creation, the same sem_t, O_EXCL, ENOENT, unlink. */
static void
test_names(void)
{
	static char long_name[NAME_MAX + 2];
	sem_t *s, *s2;

	subtest = 1;

	(void)sem_unlink(NAME1);
	if (sem_open(NAME1, 0) != SEM_FAILED || errno != ENOENT) e(1);
	if ((s = sem_open(NAME1, O_CREAT, 0600, 3)) == SEM_FAILED) e(2);
	if (value(s) != 3) e(3);
	if ((s2 = sem_open(NAME1, 0)) != s) e(4);
	/* The initial value counts only for a new one. */
	if ((s2 = sem_open(NAME1, O_CREAT, 0600, 9)) != s) e(5);
	if (value(s) != 3) e(6);
	if (sem_open(NAME1, O_CREAT | O_EXCL, 0600, 0) != SEM_FAILED ||
	    errno != EEXIST) e(7);

	if (sem_open("t147", O_CREAT, 0600, 0) != SEM_FAILED ||
	    errno != EINVAL) e(8);
	if (sem_open("/a/b", O_CREAT, 0600, 0) != SEM_FAILED ||
	    errno != EINVAL) e(9);
	/* NAME_MAX counts the slash. */
	long_name[0] = '/';
	memset(long_name + 1, 'n', NAME_MAX);
	long_name[NAME_MAX + 1] = '\0';
	if (sem_open(long_name, O_CREAT, 0600, 0) != SEM_FAILED ||
	    errno != ENAMETOOLONG) e(21);
	if (sem_unlink(long_name) != -1 || errno != ENAMETOOLONG) e(22);

	/* Unlinked, it stays usable; the name makes a new one. */
	if (sem_unlink(NAME1) != 0) e(10);
	if (sem_unlink(NAME1) != -1 || errno != ENOENT) e(11);
	if (sem_post(s) != 0 || value(s) != 4) e(12);
	if ((s2 = sem_open(NAME1, O_CREAT | O_EXCL, 0600, 1)) == SEM_FAILED)
		e(13);
	if (s2 == s || value(s2) != 1 || value(s) != 4) e(14);

	/* Opened three times: three closes. */
	if (sem_close(s) != 0 || sem_close(s) != 0) e(15);
	if (sem_post(s) != 0) e(16);		/* still open */
	if (sem_close(s) != 0) e(17);
	if (sem_close(s) != -1 || errno != EINVAL) e(18);
	if (sem_close(s2) != 0) e(19);
	if (sem_unlink(NAME1) != 0) e(20);
}

/* Waiting and posting between processes. */
static void
test_wait_post(void)
{
	sem_t *s, *done;
	struct timespec ts;
	struct timeval t0, t1;
	pid_t pid;
	long ms;
	int status, i;

	subtest = 2;

	(void)sem_unlink(NAME1);
	(void)sem_unlink(NAME2);
	if ((s = sem_open(NAME1, O_CREAT | O_EXCL, 0600, 0)) == SEM_FAILED)
		e(1);
	if (sem_trywait(s) != -1 || errno != EAGAIN) e(2);

	/* The child opens it by name; the parent waits. */
	if ((pid = fork()) == 0) {
		sem_t *c;

		usleep(100000);
		if ((c = sem_open(NAME1, 0)) == SEM_FAILED)
			_exit(1);
		for (i = 0; i < 3; i++)
			if (sem_post(c) != 0)
				_exit(2);
		_exit(0);
	}
	if (pid < 0) e(3);
	for (i = 0; i < 3; i++)
		if (sem_wait(s) != 0) e(4);
	if (waitpid(pid, &status, 0) != pid || status != 0) e(5);
	if (value(s) != 0) e(6);

	/* The sem_t from sem_open() works in a child of fork() too. */
	if ((done = sem_open(NAME2, O_CREAT | O_EXCL, 0600, 0)) == SEM_FAILED)
		e(7);
	if ((pid = fork()) == 0) {
		if (sem_wait(s) != 0)
			_exit(1);
		if (sem_post(done) != 0)
			_exit(2);
		_exit(0);
	}
	if (pid < 0) e(8);
	usleep(100000);
	if (sem_post(s) != 0) e(9);
	if (sem_wait(done) != 0) e(10);
	if (waitpid(pid, &status, 0) != pid || status != 0) e(11);

	/* Timeouts. */
	deadline(&ts, 200);
	gettimeofday(&t0, NULL);
	if (sem_timedwait(s, &ts) != -1 || errno != ETIMEDOUT) e(12);
	gettimeofday(&t1, NULL);
	ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_usec - t0.tv_usec) / 1000;
	if (ms < 150 || ms > 5000) e(13);
	deadline(&ts, -1000);			/* in the past */
	if (sem_timedwait(s, &ts) != -1 || errno != ETIMEDOUT) e(14);
	ts.tv_nsec = 1000000000L;
	if (sem_timedwait(s, &ts) != -1 || errno != EINVAL) e(15);
	/* ... only when it would block. */
	if (sem_post(s) != 0) e(16);
	if (sem_timedwait(s, &ts) != 0) e(17);

	/* A caught signal interrupts the wait. */
	signal(SIGUSR1, catcher);
	if ((pid = fork()) == 0) {
		usleep(100000);
		kill(getppid(), SIGUSR1);
		_exit(0);
	}
	if (pid < 0) e(18);
	if (sem_wait(s) != -1 || errno != EINTR) e(19);
	if (waitpid(pid, &status, 0) != pid || status != 0) e(20);
	signal(SIGUSR1, SIG_DFL);

	if (sem_close(s) != 0 || sem_close(done) != 0) e(21);
	if (sem_unlink(NAME1) != 0 || sem_unlink(NAME2) != 0) e(22);
}

/* An unnamed semaphore in shared memory, between processes. */
static void
test_pshared(void)
{
	sem_t *s;
	pid_t pid;
	int fd, status, i;
	long n;

	subtest = 3;

	(void)shm_unlink("/t147_shm");
	if ((fd = shm_open("/t147_shm", O_RDWR | O_CREAT | O_EXCL, 0600)) < 0)
		e(1);
	if (ftruncate(fd, sysconf(_SC_PAGESIZE)) != 0) e(2);
	s = mmap(NULL, sysconf(_SC_PAGESIZE), PROT_READ | PROT_WRITE,
	    MAP_SHARED, fd, 0);
	if (s == MAP_FAILED) e(3);
	if (sem_init(s, 1, 0) != 0) e(4);

	if ((pid = fork()) == 0) {
		for (i = 0; i < 5; i++)
			if (sem_post(s) != 0)
				_exit(1);
		_exit(0);
	}
	if (pid < 0) e(5);
	for (i = 0; i < 5; i++)
		if (sem_wait(s) != 0) e(6);
	if (waitpid(pid, &status, 0) != pid || status != 0) e(7);
	if (value(s) != 0) e(8);
	if (sem_destroy(s) != 0) e(9);
	if (sem_wait(s) != -1 || errno != EINVAL) e(10);

	/* A process-private one still works as before. */
	if (sem_init(s, 0, 2) != 0) e(11);
	if (sem_trywait(s) != 0 || value(s) != 1) e(12);
	if (sem_destroy(s) != 0) e(13);

	munmap(s, sysconf(_SC_PAGESIZE));
	close(fd);
	shm_unlink("/t147_shm");

	if (sysconf(_SC_SEMAPHORES) <= 0) e(14);
	/* No limit (-1), or at least the POSIX minimum. */
	errno = 0;
	n = sysconf(_SC_SEM_NSEMS_MAX);
	if (n == -1 ? errno != 0 : n < _POSIX_SEM_NSEMS_MAX) e(15);
}

int
main(int argc, char **argv)
{
	start(147);

	test_names();
	test_wait_post();
	test_pshared();

	quit();
	return 0;
}
