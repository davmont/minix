/* Test 169 - deferred pthread_cancel(3) and the cancellation points.
 *
 * A thread whose cancellation is deferred, the default, is cancelled when it
 * reaches a cancellation point, and also while blocked in one: read(2),
 * write(2), open(2) of a FIFO, select(2), poll(2), sleep(3), nanosleep(2),
 * clock_nanosleep(2), pause(3), sigwait(3), waitpid(2), recv(2) and
 * accept(2); its cleanup handlers run.  A cancellation already pending stops
 * the call before it does anything.  A thread that has disabled cancellation
 * is not interrupted, and is cancelled at its next cancellation point once it
 * enables it again.
 */
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

static volatile int running, cleaned;
static int fds[2];
static pid_t child;

static void
sleep_ms(int ms)
{
	struct timespec ts;

	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (ms % 1000) * 1000000L;
	while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
		;
}

static void
on_cancel(void *arg)
{
	cleaned = 1;
}

/* What the thread blocks in. */
enum { READ, WRITE, FIFO, SELECT, POLL, SLEEP, NANOSLEEP, CLOCK_NANOSLEEP,
	PAUSE, SIGWAIT, WAITPID, RECV, ACCEPT, NCALLS };
static const char *names[NCALLS] = { "read", "write", "fifo", "select",
	"poll", "sleep", "nanosleep", "clock_nanosleep", "pause", "sigwait",
	"waitpid", "recv", "accept" };

static void
block_in(int call)
{
	char buf[512];
	struct timespec ts = { 30, 0 };
	struct pollfd pfd;
	fd_set set;
	sigset_t sigs;
	int sig;

	switch (call) {
	case READ:
		(void)read(fds[0], buf, 1);
		break;
	case WRITE:
		/* The pipe is full: see setup(). */
		(void)write(fds[1], buf, sizeof(buf));
		break;
	case FIFO:
		(void)open("fifo", O_RDONLY);	/* no writer */
		break;
	case SELECT:
		FD_ZERO(&set);
		FD_SET(fds[0], &set);
		(void)select(fds[0] + 1, &set, NULL, NULL, NULL);
		break;
	case POLL:
		pfd.fd = fds[0];
		pfd.events = POLLIN;
		(void)poll(&pfd, 1, -1);
		break;
	case SLEEP:
		(void)sleep(30);
		break;
	case NANOSLEEP:
		(void)nanosleep(&ts, NULL);
		break;
	case CLOCK_NANOSLEEP:
		(void)clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
		break;
	case PAUSE:
		(void)pause();
		break;
	case SIGWAIT:
		sigemptyset(&sigs);
		sigaddset(&sigs, SIGUSR1);
		(void)pthread_sigmask(SIG_BLOCK, &sigs, NULL);
		(void)sigwait(&sigs, &sig);
		break;
	case WAITPID:
		(void)waitpid(child, NULL, 0);
		break;
	case RECV:
	case ACCEPT:
		/* fds[0] is a socket here: see setup(). */
		if (call == RECV)
			(void)recv(fds[0], buf, 1, 0);
		else
			(void)accept(fds[0], NULL, NULL);
		break;
	}
}

static void *
blocker(void *arg)
{

	pthread_cleanup_push(on_cancel, NULL);
	running = 1;
	block_in((int)(long)arg);
	pthread_cleanup_pop(0);
	return NULL;			/* not cancelled */
}

static void
setup(int call)
{
	struct sockaddr_un sun;
	int fl;

	switch (call) {
	case FIFO:
		if (mkfifo("fifo", 0600) != 0) e(90);
		break;
	case RECV:
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) e(91);
		break;
	case ACCEPT:
		memset(&sun, 0, sizeof(sun));
		sun.sun_family = AF_UNIX;
		strlcpy(sun.sun_path, "sock", sizeof(sun.sun_path));
		if ((fds[0] = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) e(92);
		if (bind(fds[0], (struct sockaddr *)&sun, sizeof(sun)) != 0 ||
		    listen(fds[0], 1) != 0) e(93);
		fds[1] = -1;
		break;
	case WAITPID:
		if ((child = fork()) == -1) e(94);
		if (child == 0) {
			sleep(30);
			_exit(0);
		}
		/* FALLTHROUGH */
	default:
		if (pipe(fds) != 0) e(95);
		if (call == WRITE) {
			/* Fill it. */
			fl = fcntl(fds[1], F_GETFL);
			(void)fcntl(fds[1], F_SETFL, fl | O_NONBLOCK);
			while (write(fds[1], "x", 1) == 1)
				;
			(void)fcntl(fds[1], F_SETFL, fl);
		}
		break;
	}
}

static void
teardown(int call)
{
	int status;

	close(fds[0]);
	if (fds[1] != -1)
		close(fds[1]);
	if (call == FIFO) (void)unlink("fifo");
	if (call == ACCEPT) (void)unlink("sock");
	if (call == WAITPID) {
		if (kill(child, SIGKILL) != 0) e(96);
		if (waitpid(child, &status, 0) != child) e(97);
	}
}

/* Cancel a thread blocked in each call. */
static void
test169a(void)
{
	pthread_t t;
	void *res;
	int call;

	subtest = 1;

	for (call = 0; call < NCALLS; call++) {
		setup(call);
		running = cleaned = 0;
		if (pthread_create(&t, NULL, blocker, (void *)(long)call) != 0)
			e(1);
		while (!running)
			sleep_ms(10);
		sleep_ms(200);			/* until it blocks */
		alarm(20);			/* a hang fails the test */
		if (pthread_cancel(t) != 0) e(2);
		if (pthread_join(t, &res) != 0) e(3);
		alarm(0);
		if (res != PTHREAD_CANCELED || !cleaned) {
			printf("not cancelled in %s\n", names[call]);
			e(4);
		}
		teardown(call);
	}
}

/* A cancellation already pending stops read(2) before it reads. */
static volatile int go;

static void *
pending(void *arg)
{
	char c;

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	running = 1;
	while (!go)
		;				/* no cancellation point */
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void)read(fds[0], &c, 1);		/* there is a byte to read */
	return NULL;
}

static void
test169b(void)
{
	pthread_t t;
	void *res;
	char c;

	subtest = 2;

	if (pipe(fds) != 0) e(1);
	if (write(fds[1], "p", 1) != 1) e(2);
	running = go = 0;
	if (pthread_create(&t, NULL, pending, NULL) != 0) e(3);
	while (!running)
		sleep_ms(10);
	if (pthread_cancel(t) != 0) e(4);
	go = 1;
	if (pthread_join(t, &res) != 0 || res != PTHREAD_CANCELED) e(5);
	/* The byte is still there. */
	if (fcntl(fds[0], F_SETFL, O_NONBLOCK) != 0) e(6);
	if (read(fds[0], &c, 1) != 1 || c != 'p') e(7);
	close(fds[0]);
	close(fds[1]);
}

/* A thread with cancellation disabled is not interrupted, and is cancelled at
 * its next cancellation point once it enables it. */
static volatile ssize_t got;

static void *
disabled(void *arg)
{
	char c;

	pthread_cleanup_push(on_cancel, NULL);
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	running = 1;
	got = read(fds[0], &c, 1);
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	pthread_testcancel();
	pthread_cleanup_pop(0);
	return NULL;
}

static void
test169c(void)
{
	pthread_t t;
	void *res;

	subtest = 3;

	if (pipe(fds) != 0) e(1);
	running = cleaned = 0;
	got = -2;
	if (pthread_create(&t, NULL, disabled, NULL) != 0) e(2);
	while (!running)
		sleep_ms(10);
	sleep_ms(200);
	if (pthread_cancel(t) != 0) e(3);
	sleep_ms(200);
	if (got != -2) e(4);			/* still in read(2) */
	if (write(fds[1], "d", 1) != 1) e(5);
	alarm(20);
	if (pthread_join(t, &res) != 0) e(6);
	alarm(0);
	if (got != 1) e(7);
	if (res != PTHREAD_CANCELED || !cleaned) e(8);
	close(fds[0]);
	close(fds[1]);
}

/* A thread cancelled in a cancellation point leaves no trace in the next
 * thread, which may reuse its structure: a deferred cancellation of the new
 * one waits for its cancellation point. */
static volatile int spun;

static void *
spinner(void *arg)
{
	running = 1;
	while (!go)
		spun++;				/* no cancellation point */
	pthread_testcancel();
	return NULL;
}

static void
test169d(void)
{
	pthread_t t;
	void *res;
	int i;

	subtest = 4;

	for (i = 0; i < 5; i++) {
		setup(READ);
		running = 0;
		if (pthread_create(&t, NULL, blocker, (void *)(long)READ) != 0)
			e(1);
		while (!running)
			sleep_ms(10);
		sleep_ms(50);
		if (pthread_cancel(t) != 0) e(2);
		if (pthread_join(t, &res) != 0 || res != PTHREAD_CANCELED)
			e(3);
		teardown(READ);

		running = go = 0;
		if (pthread_create(&t, NULL, spinner, NULL) != 0) e(4);
		while (!running)
			sleep_ms(10);
		if (pthread_cancel(t) != 0) e(5);
		sleep_ms(50);
		spun = 0;
		sleep_ms(50);
		if (spun == 0) e(6);		/* it runs on */
		go = 1;
		if (pthread_join(t, &res) != 0 || res != PTHREAD_CANCELED)
			e(7);
	}
}

int
main(int argc, char **argv)
{

	start(169);

	test169a();
	test169b();
	test169c();
	test169d();

	quit();
	return 0;
}
