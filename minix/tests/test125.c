/* Test 125 - POSIX message queues: mq_open(3) and co.
 *
 * Names and creation, attributes, priority order, message sizes, blocking and
 * non-blocking sends and receives across processes, timeouts, interruption
 * by a signal, notification (SI_MESGQ with the sigev_value, once, only for a
 * message arriving at an empty queue, one process at a time), access modes,
 * mq_unlink() leaving open descriptors working, fork and exec.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <mqueue.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

static char name1[64], name2[64];
static char self[1024];

static void
watchdog(int sig)
{

	printf("timed out in subtest %d\n", subtest);
	fflush(stdout);
	_exit(1);
}

static mqd_t
make(const char *name, int oflag, long maxmsg, long msgsize)
{
	struct mq_attr a;
	mqd_t q;

	memset(&a, 0, sizeof(a));
	a.mq_maxmsg = maxmsg;
	a.mq_msgsize = msgsize;
	q = mq_open(name, oflag | O_CREAT, 0600, maxmsg ? &a : NULL);
	if (q == (mqd_t)-1) e(90);
	return q;
}

static long
now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void
in(struct timespec *ts, long ms)
{

	clock_gettime(CLOCK_REALTIME, ts);
	ts->tv_sec += ms / 1000;
	ts->tv_nsec += (ms % 1000) * 1000000L;
	if (ts->tv_nsec >= 1000000000L) {
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000L;
	}
}

static void
test_open(void)
{
	struct mq_attr a;
	mqd_t q, q2;

	subtest = 1;
	mq_unlink(name1);
	if (mq_open(name1, O_RDWR) != (mqd_t)-1 || errno != ENOENT) e(1);
	if (mq_open("noslash", O_RDWR | O_CREAT, 0600, NULL) != (mqd_t)-1 ||
	    errno != EINVAL) e(2);
	if (mq_open("/a/b", O_RDWR | O_CREAT, 0600, NULL) != (mqd_t)-1 ||
	    errno != EINVAL) e(3);
	memset(&a, 0, sizeof(a));
	a.mq_maxmsg = 0;
	a.mq_msgsize = 16;
	if (mq_open(name1, O_RDWR | O_CREAT, 0600, &a) != (mqd_t)-1 ||
	    errno != EINVAL) e(4);
	a.mq_maxmsg = 1000000;
	if (mq_open(name1, O_RDWR | O_CREAT, 0600, &a) != (mqd_t)-1 ||
	    errno != EINVAL) e(5);

	q = make(name1, O_RDWR, 4, 32);
	if (mq_getattr(q, &a) != 0) e(6);
	if (a.mq_maxmsg != 4 || a.mq_msgsize != 32 || a.mq_curmsgs != 0 ||
	    a.mq_flags != 0) e(7);
	if (mq_open(name1, O_RDWR | O_CREAT | O_EXCL, 0600, NULL) !=
	    (mqd_t)-1 || errno != EEXIST) e(8);
	/* Opening it again ignores the attributes. */
	a.mq_maxmsg = 8;
	if ((q2 = mq_open(name1, O_RDWR | O_CREAT, 0600, &a)) == (mqd_t)-1)
		e(9);
	if (mq_getattr(q2, &a) != 0 || a.mq_maxmsg != 4) e(10);
	if (mq_close(q2) != 0) e(11);
	if (mq_close(q) != 0) e(12);
	if (mq_close(q) != -1 || errno != EBADF) e(13);

	/* Default attributes. */
	q = mq_open(name2, O_RDWR | O_CREAT | O_EXCL, 0600, NULL);
	if (q == (mqd_t)-1) e(14);
	if (mq_getattr(q, &a) != 0 || a.mq_maxmsg <= 0 || a.mq_msgsize <= 0)
		e(15);
	mq_close(q);
	if (mq_unlink(name2) != 0) e(16);
	if (mq_unlink(name2) != -1 || errno != ENOENT) e(17);
}

static void
test_messages(void)
{
	char buf[64];
	unsigned prio;
	struct mq_attr a, old;
	mqd_t q;

	subtest = 2;
	q = make(name1, O_RDWR, 4, 32);
	/* Highest priority first; FIFO within a priority. */
	if (mq_send(q, "low1", 5, 1) != 0) e(1);
	if (mq_send(q, "high", 5, 9) != 0) e(2);
	if (mq_send(q, "low2", 5, 1) != 0) e(3);
	if (mq_send(q, "zero", 5, 0) != 0) e(4);
	if (mq_getattr(q, &a) != 0 || a.mq_curmsgs != 4) e(5);
	if (mq_receive(q, buf, sizeof(buf), &prio) != 5 ||
	    strcmp(buf, "high") || prio != 9) e(6);
	if (mq_receive(q, buf, sizeof(buf), &prio) != 5 ||
	    strcmp(buf, "low1") || prio != 1) e(7);
	if (mq_receive(q, buf, sizeof(buf), &prio) != 5 ||
	    strcmp(buf, "low2")) e(8);
	if (mq_receive(q, buf, sizeof(buf), NULL) != 5 ||
	    strcmp(buf, "zero")) e(9);

	subtest = 3;
	/* Sizes and priorities. */
	memset(buf, 'x', sizeof(buf));
	if (mq_send(q, buf, 33, 0) != -1 || errno != EMSGSIZE) e(1);
	if (mq_send(q, buf, 32, 0) != 0) e(2);
	if (mq_send(q, buf, 0, 0) != 0) e(3);		/* empty message */
	if (mq_send(q, buf, 1, MQ_PRIO_MAX) != -1 || errno != EINVAL) e(4);
	if (mq_receive(q, buf, 31, NULL) != -1 || errno != EMSGSIZE) e(5);
	if (mq_receive(q, buf, 32, NULL) != 32) e(6);
	if (mq_receive(q, buf, 32, NULL) != 0) e(7);

	subtest = 4;
	/* O_NONBLOCK through mq_setattr(): EAGAIN both ways. */
	memset(&a, 0, sizeof(a));
	a.mq_flags = O_NONBLOCK;
	if (mq_setattr(q, &a, &old) != 0 || old.mq_flags != 0) e(1);
	if (mq_getattr(q, &a) != 0 || a.mq_flags != O_NONBLOCK) e(2);
	if (mq_receive(q, buf, 32, NULL) != -1 || errno != EAGAIN) e(3);
	while (mq_send(q, "f", 1, 0) == 0)
		;
	if (errno != EAGAIN) e(4);
	if (mq_getattr(q, &a) != 0 || a.mq_curmsgs != 4) e(5);
	mq_close(q);
	mq_unlink(name1);
}

static void
test_blocking(void)
{
	struct timespec ts;
	char buf[32];
	mqd_t q;
	pid_t pid;
	long t0;
	int status;

	subtest = 5;
	q = make(name1, O_RDWR, 2, 32);
	/* A receive waits for another process's send. */
	if ((pid = fork()) < 0) e(1);
	if (pid == 0) {
		usleep(300000);
		_exit(mq_send(q, "hi", 3, 4) == 0 ? 0 : 1);
	}
	t0 = now_ms();
	if (mq_receive(q, buf, sizeof(buf), NULL) != 3 || strcmp(buf, "hi"))
		e(2);
	if (now_ms() - t0 < 200) e(3);
	if (waitpid(pid, &status, 0) != pid || WEXITSTATUS(status) != 0) e(4);

	subtest = 6;
	/* A send to a full queue waits for a receive. */
	if (mq_send(q, "1", 2, 0) != 0 || mq_send(q, "2", 2, 0) != 0) e(1);
	if ((pid = fork()) < 0) e(2);
	if (pid == 0) {
		usleep(300000);
		_exit(mq_receive(q, buf, sizeof(buf), NULL) == 2 ? 0 : 1);
	}
	t0 = now_ms();
	if (mq_send(q, "3", 2, 0) != 0) e(3);
	if (now_ms() - t0 < 200) e(4);
	if (waitpid(pid, &status, 0) != pid || WEXITSTATUS(status) != 0) e(5);
	while (mq_receive(q, buf, sizeof(buf), NULL) > 0) {
		struct mq_attr a;

		if (mq_getattr(q, &a) == 0 && a.mq_curmsgs == 0) break;
	}

	subtest = 7;
	/* Timeouts. */
	in(&ts, 300);
	t0 = now_ms();
	if (mq_timedreceive(q, buf, sizeof(buf), NULL, &ts) != -1 ||
	    errno != ETIMEDOUT) e(1);
	if (now_ms() - t0 < 200) e(2);
	ts.tv_nsec = 1000000000L;
	if (mq_timedreceive(q, buf, sizeof(buf), NULL, &ts) != -1 ||
	    errno != EINVAL) e(3);
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec -= 10;			/* passed: at once */
	if (mq_timedreceive(q, buf, sizeof(buf), NULL, &ts) != -1 ||
	    errno != ETIMEDOUT) e(4);
	/* With a message there, the timeout does not matter. */
	if (mq_send(q, "m", 2, 0) != 0) e(5);
	ts.tv_nsec = -1;
	if (mq_timedreceive(q, buf, sizeof(buf), NULL, &ts) != 2) e(6);
	mq_close(q);
	mq_unlink(name1);
}

static void
on_usr1(int sig)
{
}

static volatile sig_atomic_t got, got_code;
static void * volatile got_value;

static void
on_notify(int sig, siginfo_t *si, void *ctx)
{

	got++;
	got_code = si->si_code;
	got_value = si->si_value.sival_ptr;
}

static int marker;

static void
test_signals(void)
{
	struct sigaction sa;
	struct sigevent ev;
	char buf[32];
	mqd_t q;
	pid_t pid;
	int status;

	subtest = 8;
	q = make(name1, O_RDWR, 2, 32);
	/* A signal interrupts a blocked receive. */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_usr1;
	sigaction(SIGUSR1, &sa, NULL);
	if ((pid = fork()) < 0) e(1);
	if (pid == 0) {
		usleep(300000);
		kill(getppid(), SIGUSR1);
		_exit(0);
	}
	if (mq_receive(q, buf, sizeof(buf), NULL) != -1 || errno != EINTR)
		e(2);
	waitpid(pid, &status, 0);

	subtest = 9;
	/* Notification: once, with SI_MESGQ and the value. */
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = on_notify;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGUSR2, &sa, NULL);
	memset(&ev, 0, sizeof(ev));
	ev.sigev_notify = SIGEV_SIGNAL;
	ev.sigev_signo = SIGUSR2;
	ev.sigev_value.sival_ptr = &marker;
	got = 0;
	if (mq_notify(q, &ev) != 0) e(1);
	if (mq_notify(q, &ev) != -1 || errno != EBUSY) e(2);
	if ((pid = fork()) < 0) e(3);
	if (pid == 0) {
		/* Another process cannot register either. */
		_exit(mq_notify(q, &ev) == -1 && errno == EBUSY ? 0 : 1);
	}
	if (waitpid(pid, &status, 0) != pid || WEXITSTATUS(status) != 0) e(4);
	if (mq_send(q, "a", 2, 0) != 0) e(5);
	usleep(100000);
	if (got != 1 || got_code != SI_MESGQ || got_value != &marker) e(6);
	/* Used up: a later message to the empty queue says nothing. */
	mq_receive(q, buf, sizeof(buf), NULL);
	if (mq_send(q, "b", 2, 0) != 0) e(7);
	usleep(100000);
	if (got != 1) e(8);

	subtest = 10;
	/* Only for an empty queue; and unregistering works. */
	if (mq_notify(q, &ev) != 0) e(1);	/* the queue holds "b" */
	if (mq_send(q, "c", 2, 0) != 0) e(2);
	usleep(100000);
	if (got != 1) e(3);
	if (mq_notify(q, NULL) != 0) e(4);
	if (mq_notify(q, &ev) != 0) e(5);	/* free again */
	if (mq_notify(q, NULL) != 0) e(6);
	ev.sigev_notify = SIGEV_THREAD;
	if (mq_notify(q, &ev) != -1 || errno != EINVAL) e(7);
	mq_close(q);
	mq_unlink(name1);
	signal(SIGUSR1, SIG_DFL);
	signal(SIGUSR2, SIG_DFL);
}

static void
test_access(void)
{
	char buf[32], arg[16];
	mqd_t q, r, w;
	pid_t pid;
	int status;

	subtest = 11;
	q = make(name1, O_RDWR, 4, 32);
	if ((r = mq_open(name1, O_RDONLY)) == (mqd_t)-1) e(1);
	if ((w = mq_open(name1, O_WRONLY)) == (mqd_t)-1) e(2);
	if (mq_send(r, "x", 2, 0) != -1 || errno != EBADF) e(3);
	if (mq_receive(w, buf, sizeof(buf), NULL) != -1 || errno != EBADF)
		e(4);
	if (mq_send(w, "x", 2, 0) != 0) e(5);
	if (mq_receive(r, buf, sizeof(buf), NULL) != 2) e(6);
	if (mq_send(-1, "x", 2, 0) != -1 || errno != EBADF) e(7);
	mq_close(r);
	mq_close(w);

	subtest = 12;
	/* Unlinked: the name is gone, open descriptors still work. */
	if (mq_unlink(name1) != 0) e(1);
	if (mq_open(name1, O_RDWR) != (mqd_t)-1 || errno != ENOENT) e(2);
	if (mq_send(q, "still", 6, 0) != 0) e(3);
	if (mq_receive(q, buf, sizeof(buf), NULL) != 6) e(4);
	/* A new queue under the name is a different one. */
	r = make(name1, O_RDWR, 4, 32);
	if (mq_send(q, "old", 4, 0) != 0) e(5);
	{
		struct mq_attr a;

		if (mq_getattr(r, &a) != 0 || a.mq_curmsgs != 0) e(6);
	}
	mq_close(r);

	subtest = 13;
	/* fork: the child has the descriptor; exec: it is closed. */
	if ((pid = fork()) < 0) e(1);
	if (pid == 0)
		_exit(mq_receive(q, buf, sizeof(buf), NULL) == 4 ? 0 : 1);
	if (waitpid(pid, &status, 0) != pid || WEXITSTATUS(status) != 0) e(2);
	if ((pid = fork()) < 0) e(3);
	if (pid == 0) {
		snprintf(arg, sizeof(arg), "%d", (int)q);
		execl(self, self, "exec-check", arg, (char *)NULL);
		_exit(2);
	}
	if (waitpid(pid, &status, 0) != pid || WEXITSTATUS(status) != 0) e(4);
	mq_close(q);
	mq_unlink(name1);
}

int
main(int argc, char **argv)
{
	struct mq_attr a;

	if (argc == 3 && !strcmp(argv[1], "exec-check"))
		exit(mq_getattr(atoi(argv[2]), &a) == -1 && errno == EBADF ?
		    0 : 1);

	if (argv[0][0] == '/')
		strlcpy(self, argv[0], sizeof(self));
	else if (getcwd(self, sizeof(self)) != NULL) {
		strlcat(self, "/", sizeof(self));
		strlcat(self, argv[0], sizeof(self));
	}

	start(125);
	snprintf(name1, sizeof(name1), "/test125-%d-a", (int)getpid());
	snprintf(name2, sizeof(name2), "/test125-%d-b", (int)getpid());
	signal(SIGALRM, watchdog);
	alarm(60);

	test_open();
	test_messages();
	test_blocking();
	test_signals();
	test_access();

	alarm(0);
	quit();
	return 0;
}
