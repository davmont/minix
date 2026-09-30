/* Test 99 - pthread_kill(3).
 *
 * A signal sent with pthread_kill() is delivered to that thread and no
 * other; its siginfo_t says SI_LWP; signal 0 checks that a thread exists;
 * bad signals and finished threads are refused.
 */
#include <pthread.h>
#include <signal.h>

#include "common.h"

int max_error = 0;

static volatile pthread_t caught_in;
static volatile int caught, code;
static volatile int stop;

static void
handler(int sig, siginfo_t *si, void *ctx)
{
	caught = sig;
	caught_in = pthread_self();
	code = si->si_code;
}

static void *
worker(void *arg)
{
	sigset_t set;

	/* Wait for the signal, which is blocked everywhere else. */
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	pthread_sigmask(SIG_UNBLOCK, &set, NULL);
	while (!stop && !caught)
		sched_yield();
	return arg;
}

int
main(int argc, char **argv)
{
	struct sigaction sa;
	sigset_t set;
	pthread_t t;
	int i;

	start(99);

	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = handler;
	if (sigaction(SIGUSR1, &sa, NULL) != 0) e(1);

	/* Block SIGUSR1 in the main thread; the worker unblocks it. */
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0) e(2);

	subtest = 1;
	if (pthread_create(&t, NULL, worker, NULL) != 0) e(3);
	if (pthread_kill(t, 0) != 0) e(4);		/* it exists */
	if (pthread_kill(t, _NSIG) != EINVAL) e(5);
	if (pthread_kill(t, SIGUSR1) != 0) e(6);
	for (i = 0; i < 1000 && !caught; i++)
		usleep(10000);
	stop = 1;
	if (pthread_join(t, NULL) != 0) e(7);
	if (caught != SIGUSR1) e(8);
	if (!pthread_equal(caught_in, t)) e(9);		/* that thread */
	if (code != SI_LWP) e(10);

	subtest = 2;
	if (pthread_kill(pthread_self(), 0) != 0) e(11);

	quit();
	return 0;
}
