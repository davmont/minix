/* Test 151 - POSIX asynchronous I/O (librt).
 *
 * aio_read() and aio_write() at offsets, aio_error() and aio_return() as POSIX
 * has them (EINPROGRESS, once only, EINVAL), the errors a submission fails
 * with, aio_fsync(), aio_cancel() of queued requests and of running ones,
 * aio_suspend() with a timeout and interrupted by a signal, lio_listio() in
 * both modes, the three kinds of notification, and fork().
 *
 * Reads of an empty pipe keep requests running, and those behind them on
 * the same descriptor queued (librt runs one request at a time on each),
 * for as long as the test needs.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <aio.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

#define BLOCKERS	20	/* requests on the pipe */
#define MANY		200	/* completion signals pending at once */

static char file[] = "t151.dat";
static volatile sig_atomic_t got_sig, got_value;
static volatile int thread_value;
static pthread_mutex_t thread_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t thread_cv = PTHREAD_COND_INITIALIZER;

static void
on_signal(int sig, siginfo_t *si, void *ctx)
{
	got_sig = sig;
	got_value = si->si_value.sival_int;
}

static void
on_alarm(int sig)
{
}

static void
on_thread(union sigval v)
{
	pthread_mutex_lock(&thread_lock);
	thread_value = v.sival_int;
	pthread_cond_signal(&thread_cv);
	pthread_mutex_unlock(&thread_lock);
}

/* Wait for a request with aio_suspend(), then collect it. */
static ssize_t
collect(struct aiocb *cb)
{
	const struct aiocb *list[1];

	list[0] = cb;
	while (aio_error(cb) == EINPROGRESS)
		if (aio_suspend(list, 1, NULL) != 0 && errno != EINTR)
			return -2;
	return aio_return(cb);
}

static void
setup(struct aiocb *cb, int fd, void *buf, size_t n, off_t off)
{
	memset(cb, 0, sizeof(*cb));
	cb->aio_fildes = fd;
	cb->aio_buf = buf;
	cb->aio_nbytes = n;
	cb->aio_offset = off;
	cb->aio_sigevent.sigev_notify = SIGEV_NONE;
}

static int
open_file(void)
{
	int fd;

	(void)unlink(file);
	if ((fd = open(file, O_RDWR | O_CREAT | O_TRUNC, 0600)) < 0)
		e(0);
	return fd;
}

/* Reads and writes, and what aio_error() and aio_return() say. */
static void
test151a(void)
{
	struct aiocb cb, cb2, cb3;
	char out[100], in[100];
	int fd, i;

	subtest = 1;
	fd = open_file();
	for (i = 0; i < (int)sizeof(out); i++)
		out[i] = (char)i;

	memset(&cb, 0, sizeof(cb));
	if (aio_error(&cb) != -1 || errno != EINVAL) e(1);
	if (aio_return(&cb) != -1 || errno != EINVAL) e(2);

	/* Two writes, the second one at an offset before the first one. */
	setup(&cb, fd, out + 50, 50, 50);
	setup(&cb2, fd, out, 50, 0);
	if (aio_write(&cb) != 0) e(3);
	if (aio_write(&cb2) != 0) e(4);
	if (collect(&cb) != 50) e(5);
	if (collect(&cb2) != 50) e(6);
	if (aio_error(&cb) != 0) e(7);
	/* Once only. */
	if (aio_return(&cb) != -1 || errno != EINVAL) e(8);
	/* An aiocb can be used again. */
	memset(in, 0, sizeof(in));
	setup(&cb, fd, in, sizeof(in), 0);
	if (aio_read(&cb) != 0) e(9);
	if (collect(&cb) != sizeof(in)) e(10);
	if (memcmp(in, out, sizeof(in)) != 0) e(11);
	/* At end of file. */
	setup(&cb, fd, in, sizeof(in), 1000);
	if (aio_read(&cb) != 0) e(12);
	if (collect(&cb) != 0) e(13);
	if (lseek(fd, 0, SEEK_CUR) != 0) e(14);	/* the offset stays */

	/* What a submission fails with; a bad descriptor is the request's
	 * error status. */
	setup(&cb, -1, in, 1, 0);
	if (aio_read(&cb) != 0) e(15);
	if (collect(&cb) != -1 || aio_error(&cb) != EBADF) e(15);
	setup(&cb, fd, in, 1, -1);
	if (aio_read(&cb) != -1 || errno != EINVAL) e(16);
	setup(&cb, fd, in, 1, 0);
	cb.aio_reqprio = -1;
	if (aio_write(&cb) != -1 || errno != EINVAL) e(17);
	setup(&cb, fd, in, 1, 0);
	cb.aio_sigevent.sigev_notify = 1234;
	if (aio_read(&cb) != -1 || errno != EINVAL) e(18);
	setup(&cb, fd, in, 1, 0);
	if (aio_fsync(0, &cb) != -1 || errno != EINVAL) e(19);
	close(fd);
	if ((fd = open(file, O_RDONLY)) < 0) e(20);
	setup(&cb, fd, out, 1, 0);
	if (aio_write(&cb) != 0) e(21);
	if (collect(&cb) != -1 || aio_error(&cb) != EBADF) e(21);
	if (aio_fsync(O_SYNC, &cb) != -1 || errno != EBADF) e(22);
	close(fd);

	/* aio_fsync() after writes. */
	fd = open_file();
	setup(&cb, fd, out, sizeof(out), 0);
	if (aio_write(&cb) != 0) e(23);
	setup(&cb2, fd, NULL, 0, 0);
	if (aio_fsync(O_DSYNC, &cb2) != 0) e(24);
	if (collect(&cb2) != 0) e(25);
	if (aio_error(&cb) == EINPROGRESS) e(26);	/* done before it */
	if (collect(&cb) != sizeof(out)) e(27);

	/* O_APPEND: at the end, whatever the offset, and in the order the
	 * writes were made (POSIX). */
	close(fd);
	if ((fd = open(file, O_RDWR | O_APPEND)) < 0) e(28);
	memset(in, 'a', 30);
	memset(in + 30, 'b', 30);
	memset(in + 60, 'c', 30);
	setup(&cb, fd, in, 30, 0);
	setup(&cb2, fd, in + 30, 30, 0);
	setup(&cb3, fd, in + 60, 30, 0);
	if (aio_write(&cb) != 0 || aio_write(&cb2) != 0 ||
	    aio_write(&cb3) != 0) e(29);
	if (collect(&cb) != 30 || collect(&cb2) != 30 ||
	    collect(&cb3) != 30) e(30);
	if (lseek(fd, 0, SEEK_END) != sizeof(out) + 90) e(31);
	if (pread(fd, out, 90, sizeof(out)) != 90 ||
	    memcmp(out, in, 90) != 0) e(31);
	close(fd);
	(void)unlink(file);
}

/* Notification by signal and by thread. */
static void
test151b(void)
{
	struct sigaction sa;
	struct aiocb cb;
	struct timespec ts;
	static struct aiocb many[MANY];
	sigset_t set, old;
	char buf[10];
	int fd, i;

	subtest = 2;
	fd = open_file();
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = on_signal;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGRTMIN, &sa, NULL) != 0) e(1);

	/* The signal comes with its value; wait for it with it blocked. */
	sigemptyset(&set);
	sigaddset(&set, SIGRTMIN);
	sigprocmask(SIG_BLOCK, &set, &old);
	got_sig = got_value = 0;
	setup(&cb, fd, buf, sizeof(buf), 0);
	cb.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
	cb.aio_sigevent.sigev_signo = SIGRTMIN;
	cb.aio_sigevent.sigev_value.sival_int = 151;
	if (aio_write(&cb) != 0) e(2);
	ts.tv_sec = 10;
	ts.tv_nsec = 0;
	{
		siginfo_t si;

		if (sigtimedwait(&set, &si, &ts) != SIGRTMIN) e(3);
		if (si.si_value.sival_int != 151) e(4);
		if (si.si_code != SI_ASYNCIO || si.si_pid != getpid()) e(4);
	}
	if (aio_error(&cb) != 0) e(5);
	if (aio_return(&cb) != sizeof(buf)) e(6);
	sigprocmask(SIG_SETMASK, &old, NULL);

	/* Or to a handler. */
	setup(&cb, fd, buf, sizeof(buf), 0);
	cb.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
	cb.aio_sigevent.sigev_signo = SIGRTMIN;
	cb.aio_sigevent.sigev_value.sival_int = 152;
	if (aio_read(&cb) != 0) e(7);
	if (collect(&cb) != sizeof(buf)) e(8);
	while (got_sig == 0)
		usleep(1000);
	if (got_sig != SIGRTMIN || got_value != 152) e(9);

	/* By a new thread. */
	thread_value = 0;
	setup(&cb, fd, buf, sizeof(buf), 0);
	cb.aio_sigevent.sigev_notify = SIGEV_THREAD;
	cb.aio_sigevent.sigev_notify_function = on_thread;
	cb.aio_sigevent.sigev_value.sival_int = 153;
	if (aio_read(&cb) != 0) e(10);
	pthread_mutex_lock(&thread_lock);
	while (thread_value == 0)
		pthread_cond_wait(&thread_cv, &thread_lock);
	pthread_mutex_unlock(&thread_lock);
	if (thread_value != 153) e(11);
	if (aio_return(&cb) != sizeof(buf)) e(12);

	/* More completions pending than SIGQUEUE_MAX lets sigqueue() have:
	 * none is lost. */
	sigprocmask(SIG_BLOCK, &set, &old);
	for (i = 0; i < MANY; i++) {
		setup(&many[i], fd, buf, 1, 0);
		many[i].aio_sigevent.sigev_notify = SIGEV_SIGNAL;
		many[i].aio_sigevent.sigev_signo = SIGRTMIN;
		many[i].aio_sigevent.sigev_value.sival_int = i;
		if (aio_read(&many[i]) != 0) e(13);
	}
	for (i = 0; i < MANY; i++)
		if (collect(&many[i]) != 1) e(14);
	/* A request is done before its signal is sent. */
	ts.tv_sec = 5;
	ts.tv_nsec = 0;
	for (i = 0; i < MANY; i++)
		if (sigtimedwait(&set, NULL, &ts) != SIGRTMIN) {
			e(15);
			break;
		}
	ts.tv_sec = 0;
	if (sigtimedwait(&set, NULL, &ts) != -1 || errno != EAGAIN) e(16);
	sigprocmask(SIG_SETMASK, &old, NULL);

	signal(SIGRTMIN, SIG_DFL);
	close(fd);
	(void)unlink(file);
}

/* Cancelling, and waiting with a timeout or until a signal. */
static void
test151c(void)
{
	struct aiocb blk[BLOCKERS], cb;
	const struct aiocb *list[2];
	struct timespec ts, t0, t1;
	struct sigaction sa;
	char buf[BLOCKERS][4], c = 'x';
	int p[2], fd, i, r, cancelled, running;

	subtest = 3;
	if (pipe(p) != 0) e(1);

	/* Nothing on the descriptor: all done. */
	fd = open_file();
	if (aio_cancel(fd, NULL) != AIO_ALLDONE) e(2);
	if (aio_cancel(-1, NULL) != -1 || errno != EBADF) e(3);
	setup(&cb, fd, buf[0], 1, 0);
	if (aio_cancel(p[0], &cb) != -1 || errno != EINVAL) e(4);

	/* Reads of the empty pipe: the first runs, the rest queue behind it. */
	for (i = 0; i < BLOCKERS; i++) {
		setup(&blk[i], p[0], buf[i], 1, 0);
		if (aio_read(&blk[i]) != 0) e(5);
	}
	usleep(200000);
	if (aio_error(&blk[BLOCKERS - 1]) != EINPROGRESS) e(6);

	/* Another descriptor does not wait for the pipe. */
	setup(&cb, fd, buf[0], 1, 0);
	if (aio_write(&cb) != 0) e(20);
	if (collect(&cb) != 1) e(20);

	/* A timed wait times out. */
	list[0] = NULL;		/* ignored */
	list[1] = &blk[0];
	ts.tv_sec = 0;
	ts.tv_nsec = 300000000;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	if (aio_suspend(list, 2, &ts) != -1 || errno != EAGAIN) e(7);
	clock_gettime(CLOCK_MONOTONIC, &t1);
	if (t1.tv_sec * 1000 + t1.tv_nsec / 1000000 -
	    (t0.tv_sec * 1000 + t0.tv_nsec / 1000000) < 250) e(8);

	/* A signal interrupts it. */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_alarm;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGALRM, &sa, NULL) != 0) e(9);
	alarm(1);
	if (aio_suspend(list, 2, NULL) != -1 || errno != EINTR) e(10);

	/* The last one is queued: cancelling it works, and says so. */
	if (aio_cancel(p[0], &blk[BLOCKERS - 1]) != AIO_CANCELED) e(11);
	if (aio_error(&blk[BLOCKERS - 1]) != ECANCELED) e(12);
	if (aio_return(&blk[BLOCKERS - 1]) != -1) e(13);

	/* Cancelling all: the one running cannot be. */
	if (aio_cancel(p[0], NULL) != AIO_NOTCANCELED) e(14);
	cancelled = running = 0;
	for (i = 0; i < BLOCKERS - 1; i++) {
		r = aio_error(&blk[i]);
		if (r == ECANCELED)
			cancelled++;
		else if (r == EINPROGRESS)
			running++;
		else
			e(15);
	}
	if (cancelled != BLOCKERS - 2 || running != 1) e(16);

	/* Feed the pipe until the running ones are done. */
	for (i = 0; i < running; i++)
		if (write(p[1], &c, 1) != 1) e(17);
	for (i = 0; i < BLOCKERS - 1; i++) {
		r = collect(&blk[i]);
		if (r != 1 && r != -1) e(18);
	}
	if (aio_cancel(p[0], NULL) != AIO_ALLDONE) e(19);

	close(p[0]);
	close(p[1]);
	close(fd);
	(void)unlink(file);
}

/* lio_listio() in both modes. */
static void
test151d(void)
{
	struct aiocb cbs[4], *list[5];
	struct sigaction sa;
	struct sigevent sev;
	sigset_t set, old;
	struct timespec ts;
	char out[4][16], in[4][16];
	int fd, i;

	subtest = 4;
	fd = open_file();
	for (i = 0; i < 4; i++) {
		memset(out[i], 'a' + i, sizeof(out[i]));
		setup(&cbs[i], fd, out[i], sizeof(out[i]), i * 16);
		cbs[i].aio_lio_opcode = LIO_WRITE;
		list[i] = &cbs[i];
	}
	list[4] = NULL;		/* ignored */

	if (lio_listio(1234, list, 5, NULL) != -1 || errno != EINVAL) e(1);
	if (lio_listio(LIO_WAIT, list, AIO_LISTIO_MAX + 1, NULL) != -1 ||
	    errno != EINVAL) e(2);

	/* LIO_WAIT returns when all is done. */
	if (lio_listio(LIO_WAIT, list, 5, NULL) != 0) e(3);
	for (i = 0; i < 4; i++) {
		if (aio_error(&cbs[i]) != 0) e(4);
		if (aio_return(&cbs[i]) != sizeof(out[i])) e(5);
	}

	/* LIO_NOWAIT signals when all is done; LIO_NOP is skipped. */
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = on_signal;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGUSR1, &sa, NULL) != 0) e(6);
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	sigprocmask(SIG_BLOCK, &set, &old);
	memset(in, 0, sizeof(in));
	for (i = 0; i < 4; i++) {
		setup(&cbs[i], fd, in[i], sizeof(in[i]), i * 16);
		cbs[i].aio_lio_opcode = (i == 2) ? LIO_NOP : LIO_READ;
	}
	memset(&sev, 0, sizeof(sev));
	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGUSR1;
	sev.sigev_value.sival_int = 154;
	if (lio_listio(LIO_NOWAIT, list, 4, &sev) != 0) e(7);
	ts.tv_sec = 10;
	ts.tv_nsec = 0;
	{
		siginfo_t si;

		if (sigtimedwait(&set, &si, &ts) != SIGUSR1) e(8);
		if (si.si_value.sival_int != 154) e(9);
	}
	sigprocmask(SIG_SETMASK, &old, NULL);
	for (i = 0; i < 4; i++) {
		if (i == 2) {
			if (in[i][0] != 0) e(10);
			continue;
		}
		if (aio_return(&cbs[i]) != sizeof(in[i])) e(11);
		if (memcmp(in[i], out[i], sizeof(in[i])) != 0) e(12);
	}

	/* One bad request fails alone; the call says EIO. */
	for (i = 0; i < 4; i++) {
		setup(&cbs[i], fd, in[i], sizeof(in[i]), i * 16);
		cbs[i].aio_lio_opcode = LIO_READ;
	}
	cbs[1].aio_fildes = -1;
	if (lio_listio(LIO_WAIT, list, 4, NULL) != -1 || errno != EIO) e(13);
	if (aio_error(&cbs[1]) != EBADF) e(14);
	if (aio_return(&cbs[0]) != sizeof(in[0])) e(15);
	if (aio_return(&cbs[3]) != sizeof(in[3])) e(16);

	signal(SIGUSR1, SIG_DFL);
	close(fd);
	(void)unlink(file);
}

/* A child of fork() can use AIO, also while its parent has some going. */
static void
test151e(void)
{
	struct aiocb cb, pcb;
	char buf[8], c = 'y';
	int p[2], fd, status;
	pid_t pid;

	subtest = 5;
	if (pipe(p) != 0) e(1);
	setup(&pcb, p[0], buf, 1, 0);
	if (aio_read(&pcb) != 0) e(2);		/* stays running */
	fd = open_file();

	switch (pid = fork()) {
	case -1:
		e(3);
		break;
	case 0:
		setup(&cb, fd, "abcdefgh", 8, 0);
		if (aio_write(&cb) != 0) _exit(1);
		if (collect(&cb) != 8) _exit(2);
		_exit(0);
	}
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0) e(4);
	if (pread(fd, buf, 8, 0) != 8 || memcmp(buf, "abcdefgh", 8) != 0)
		e(5);

	if (aio_error(&pcb) != EINPROGRESS) e(6);
	if (write(p[1], &c, 1) != 1) e(7);
	if (collect(&pcb) != 1) e(8);
	close(p[0]);
	close(p[1]);
	close(fd);
	(void)unlink(file);
}

static volatile int handled, handled_cancelled;

static void
on_done(int sig, siginfo_t *si, void *ctx)
{
	struct aiocb *cb = si->si_value.sival_ptr;

	/* Both async-signal-safe: they must not wait for a lock this
	 * thread holds where the signal interrupted it. */
	if (aio_error(cb) == ECANCELED)
		handled_cancelled++;
	(void)aio_return(cb);
	handled++;
}

/* Completions to a handler that collects them, while the program goes on
 * submitting, cancelling and closing (as the Open POSIX Test Suite's
 * aio_cancel/3-1 does). */
static void
test151g(void)
{
	static struct aiocb cbs[128];
	static char big[256 * 1024];
	struct sigaction sa;
	int fd, i;

	subtest = 7;
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = on_done;
	sa.sa_flags = SA_SIGINFO | SA_RESTART;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGRTMIN + 1, &sa, NULL) != 0) e(1);
	fd = open_file();
	handled = handled_cancelled = 0;
	for (i = 0; i < 128; i++) {
		setup(&cbs[i], fd, big, sizeof(big), 0);
		cbs[i].aio_sigevent.sigev_notify = SIGEV_SIGNAL;
		cbs[i].aio_sigevent.sigev_signo = SIGRTMIN + 1;
		cbs[i].aio_sigevent.sigev_value.sival_ptr = &cbs[i];
		if (aio_write(&cbs[i]) != 0) e(2);
	}
	if (aio_cancel(fd, NULL) == -1) e(3);
	close(fd);
	for (i = 0; i < 200 && handled < 128; i++)
		usleep(100000);
	if (handled != 128) e(4);
	/* Each one collected, once. */
	for (i = 0; i < 128; i++)
		if (aio_return(&cbs[i]) != -1 || errno != EINVAL) e(5);
	signal(SIGRTMIN + 1, SIG_DFL);
	(void)unlink(file);
}

static void
test151f(void)
{
	subtest = 6;
	if (sysconf(_SC_ASYNCHRONOUS_IO) != _POSIX_ASYNCHRONOUS_IO) e(1);
	if (sysconf(_SC_AIO_LISTIO_MAX) != AIO_LISTIO_MAX) e(2);
	if (sysconf(_SC_AIO_MAX) != AIO_MAX) e(3);
	if (sysconf(_SC_PRIORITIZED_IO) != -1) e(4);
	if (sysconf(_SC_AIO_PRIO_DELTA_MAX) != 0) e(5);
}

int
main(int argc, char **argv)
{

	start(151);

	test151a();
	test151b();
	test151c();
	test151d();
	test151e();
	test151f();
	test151g();

	quit();
	return 0;
}
