/* Test 165 - asynchronous pthread_cancel(3).
 *
 * A thread that is asynchronously cancelable is cancelled wherever it is: in
 * a loop of its own that calls nothing, or blocked in sleep(3), and however
 * it masks its signals; its cleanup handlers run.  A deferred one runs on
 * until it tests for the cancellation, and a disabled one until it enables
 * it.  The cancellation signal is libpthread's: sigaction() refuses it, the
 * mask calls leave it out, and sending it alone does nothing.
 */
#include <sys/types.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

static volatile int running, cleaned, after;

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

/* Asynchronous: loop without a call, or sleep; 'arg' says which, and whether
 * to block every signal first. */
#define SPIN	0x1
#define BLOCK	0x2

static void *
async(void *arg)
{
	int how = (int)(long)arg;
	sigset_t all;

	if (how & BLOCK) {
		sigfillset(&all);
		if (pthread_sigmask(SIG_BLOCK, &all, NULL) != 0) return NULL;
	}
	pthread_cleanup_push(on_cancel, NULL);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	running = 1;
	if (how & SPIN)
		for (;;)
			after++;
	else
		for (;;)
			sleep(10);
	pthread_cleanup_pop(0);
	return NULL;
}

static void
cancel_one(int how)
{
	pthread_t t;
	void *res;
	time_t t0;

	running = cleaned = after = 0;
	if (pthread_create(&t, NULL, async, (void *)(long)how) != 0) e(1);
	while (!running)
		sleep_ms(1);
	sleep_ms(20);
	t0 = time(NULL);
	if (pthread_cancel(t) != 0) e(2);
	if (pthread_join(t, &res) != 0) e(3);
	if (res != PTHREAD_CANCELED) e(4);
	if (!cleaned) e(5);
	if (time(NULL) - t0 > 3) e(6);		/* not the sleep's 10 s */
}

static void
test165a(void)
{

	subtest = 1;
	cancel_one(SPIN);
	cancel_one(0);
	cancel_one(SPIN | BLOCK);
	cancel_one(BLOCK);
}

/* Deferred: runs on until pthread_testcancel(); disabled: until enabled. */
static volatile int stop;

static void *
deferred(void *arg)
{
	int disabled = (int)(long)arg;

	if (disabled)
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	else
		pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	running = 1;
	while (!stop)
		;
	after = 1;
	if (disabled) {
		/* Asynchronous, pending: enabling it cancels. */
		pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	} else
		pthread_testcancel();
	return NULL;
}

static void
test165b(void)
{
	pthread_t t;
	void *res;
	int disabled;

	subtest = 2;
	for (disabled = 0; disabled <= 1; disabled++) {
		running = stop = after = 0;
		if (pthread_create(&t, NULL, deferred,
		    (void *)(long)disabled) != 0) e(1);
		while (!running)
			sleep_ms(1);
		if (pthread_cancel(t) != 0) e(2);
		sleep_ms(100);
		if (after) e(3);
		stop = 1;
		if (pthread_join(t, &res) != 0) e(4);
		if (res != PTHREAD_CANCELED) e(5);
		if (!after) e(6);
	}
}

/* The signal itself. */
static void *
spin(void *arg)
{

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	running = 1;
	while (!stop)
		;
	return arg;
}

static void
test165c(void)
{
	struct sigaction sa;
	sigset_t s, old;
	pthread_t t;
	void *res;

	subtest = 3;
	if (SIGRTMAX != 62 || __SIGCANCEL != 63) e(1);
	if (sysconf(_SC_RTSIG_MAX) != SIGRTMAX - SIGRTMIN + 1) e(2);

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;
	if (sigaction(__SIGCANCEL, &sa, NULL) != -1 || errno != EINVAL) e(3);
	if (sigaction(__SIGCANCEL, NULL, &sa) != -1 || errno != EINVAL) e(4);

	/* Never blocked, nor in a handler's mask. */
	sigfillset(&s);
	if (sigprocmask(SIG_BLOCK, &s, &old) != 0) e(5);
	if (sigprocmask(SIG_BLOCK, NULL, &s) != 0) e(6);
	if (sigismember(&s, __SIGCANCEL)) e(7);
	if (!sigismember(&s, SIGRTMAX)) e(8);
	if (sigprocmask(SIG_SETMASK, &old, NULL) != 0) e(9);
	sa.sa_handler = SIG_DFL;
	sigfillset(&sa.sa_mask);
	if (sigaction(SIGUSR1, &sa, NULL) != 0) e(10);
	if (sigaction(SIGUSR1, NULL, &sa) != 0) e(11);
	if (sigismember(&sa.sa_mask, __SIGCANCEL)) e(12);

	/* Sent on its own, to a thread that is asynchronously cancelable. */
	running = stop = 0;
	if (pthread_create(&t, NULL, spin, &t) != 0) e(13);
	while (!running)
		sleep_ms(1);
	if (pthread_kill(t, __SIGCANCEL) != 0) e(14);
	sleep_ms(50);
	stop = 1;
	if (pthread_join(t, &res) != 0) e(15);
	if (res != &t) e(16);
}

int
main(int argc, char **argv)
{
	start(165);

	alarm(120);
	test165a();
	test165b();
	test165c();
	alarm(0);

	quit();
	return 0;
}
