/* Test 146 - realtime signals, sigqueue(2) and sigtimedwait(2).
 *
 * Realtime signals (SIGRTMIN to SIGRTMAX) queue: each instance is delivered,
 * in order, with its sigqueue() value, the lowest-numbered signal first.
 * sigtimedwait(), sigwaitinfo() and sigwait() take a pending signal of a set,
 * or wait for one, without running a handler; a caught signal outside the set
 * interrupts the first two (sigwait() goes on waiting), and sigtimedwait()
 * gives up after its timeout.  A thread
 * in sigwait() takes a signal sent to its process.  A child of fork() and a
 * new thread start with no signals pending.  And three other things POSIX
 * says of signals: raise() signals the calling thread, sigprocmask() fails
 * with -1 and errno (the threads library had it return the error number), and
 * sigaltstack() refuses SS_DISABLE with SS_ONSTACK.
 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

#define NREC	64

static volatile int nrec;
static volatile int rec_signo[NREC];
static volatile int rec_value[NREC];
static volatile int rec_code[NREC];
static volatile pid_t rec_pid[NREC];

static void
recorder(int signo, siginfo_t *si, void *ctx)
{
	(void)ctx;
	if (nrec < NREC) {
		rec_signo[nrec] = signo;
		rec_value[nrec] = si->si_value.sival_int;
		rec_code[nrec] = si->si_code;
		rec_pid[nrec] = si->si_pid;
		nrec++;
	}
}

static volatile int caught;

static void
catcher(int signo)
{
	caught = signo;
}

static void
block(int signo, sigset_t *set)
{
	sigemptyset(set);
	sigaddset(set, signo);
	if (sigprocmask(SIG_BLOCK, set, NULL) != 0) e(1);
}

static void
unblock_all(void)
{
	sigset_t set;

	sigemptyset(&set);
	if (sigprocmask(SIG_SETMASK, &set, NULL) != 0) e(2);
}

static void
handle(int signo, void (*fn)(int, siginfo_t *, void *))
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = fn;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	if (sigaction(signo, &sa, NULL) != 0) e(3);
}

static void
dispose(int signo, void (*fn)(int))
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = fn;
	sigemptyset(&sa.sa_mask);
	if (sigaction(signo, &sa, NULL) != 0) e(4);
}

static int
queue(pid_t pid, int signo, int value)
{
	union sigval v;

	v.sival_int = value;
	return sigqueue(pid, signo, v);
}

/* What sysconf() says. */
static void
test_sysconf(void)
{
	subtest = 1;

	if (sysconf(_SC_REALTIME_SIGNALS) <= 0) e(1);
	if (sysconf(_SC_SIGQUEUE_MAX) < _POSIX_SIGQUEUE_MAX) e(2);
	if (sysconf(_SC_RTSIG_MAX) < _POSIX_RTSIG_MAX) e(3);
	if (sysconf(_SC_RTSIG_MAX) != SIGRTMAX - SIGRTMIN + 1) e(4);
	if (SIGRTMIN <= SIGPWR || SIGRTMAX >= _NSIG) e(5);
}

/* Instances queue, and come in order, lowest signal first, with values. */
static void
test_queue_handler(void)
{
	sigset_t set;
	int i;

	subtest = 2;

	handle(SIGRTMIN, recorder);
	handle(SIGRTMIN + 1, recorder);
	sigemptyset(&set);
	sigaddset(&set, SIGRTMIN);
	sigaddset(&set, SIGRTMIN + 1);
	if (sigprocmask(SIG_BLOCK, &set, NULL) != 0) e(1);

	nrec = 0;
	if (queue(getpid(), SIGRTMIN + 1, 10) != 0) e(2);
	if (queue(getpid(), SIGRTMIN, 1) != 0) e(3);
	if (queue(getpid(), SIGRTMIN, 2) != 0) e(4);
	if (queue(getpid(), SIGRTMIN, 3) != 0) e(5);
	if (sigpending(&set) != 0) e(6);
	if (!sigismember(&set, SIGRTMIN) || !sigismember(&set, SIGRTMIN + 1))
		e(7);
	if (nrec != 0) e(8);

	unblock_all();

	if (nrec != 4) e(9);
	for (i = 0; i < nrec && i < 3; i++)
		if (rec_signo[i] != SIGRTMIN || rec_value[i] != i + 1) e(10);
	if (rec_signo[3] != SIGRTMIN + 1 || rec_value[3] != 10) e(11);
	for (i = 0; i < nrec; i++)
		if (rec_code[i] != SI_QUEUE || rec_pid[i] != getpid()) e(12);

	/* kill() of a realtime signal queues as well. */
	block(SIGRTMIN, &set);
	nrec = 0;
	if (kill(getpid(), SIGRTMIN) != 0) e(13);
	if (kill(getpid(), SIGRTMIN) != 0) e(14);
	unblock_all();
	if (nrec != 2 || rec_code[0] != SI_USER || rec_code[1] != SI_USER)
		e(15);

	/* An ordinary signal does not. */
	handle(SIGUSR1, recorder);
	block(SIGUSR1, &set);
	nrec = 0;
	if (kill(getpid(), SIGUSR1) != 0) e(16);
	if (kill(getpid(), SIGUSR1) != 0) e(17);
	unblock_all();
	if (nrec != 1) e(18);

	dispose(SIGRTMIN, SIG_DFL);
	dispose(SIGRTMIN + 1, SIG_DFL);
	dispose(SIGUSR1, SIG_DFL);
}

/* sigqueue() fails with EAGAIN when the queue is full; nothing is lost. */
static void
test_queue_limit(void)
{
	sigset_t set;
	siginfo_t si;
	struct timespec zero = { 0, 0 };
	int n, i;

	subtest = 3;

	block(SIGRTMIN, &set);
	for (n = 0; n < 10000; n++)
		if (queue(getpid(), SIGRTMIN, n) != 0)
			break;
	if (n == 10000 || errno != EAGAIN) e(1);
	if (n < _POSIX_SIGQUEUE_MAX) e(2);

	for (i = 0; i < n; i++) {
		if (sigtimedwait(&set, &si, &zero) != SIGRTMIN) { e(3); break; }
		if (si.si_value.sival_int != i || si.si_code != SI_QUEUE) e(4);
	}
	if (sigtimedwait(&set, &si, &zero) != -1 || errno != EAGAIN) e(5);

	/* Ignoring the signal throws away all that is queued. */
	for (i = 0; i < 5; i++)
		if (queue(getpid(), SIGRTMIN, i) != 0) e(6);
	dispose(SIGRTMIN, SIG_IGN);
	dispose(SIGRTMIN, SIG_DFL);
	if (sigpending(&set) != 0) e(7);
	if (sigismember(&set, SIGRTMIN)) e(8);
	sigemptyset(&set);
	sigaddset(&set, SIGRTMIN);
	if (sigtimedwait(&set, &si, &zero) != -1 || errno != EAGAIN) e(9);

	if (queue(getpid(), _NSIG, 0) != -1 || errno != EINVAL) e(10);
	if (queue(-1, SIGRTMIN, 0) != -1 || errno != ESRCH) e(11);
	if (queue(getpid(), 0, 0) != 0) e(12);	/* only checks */

	unblock_all();
}

/* sigtimedwait() polls, times out, and rejects a bad timeout. */
static void
test_timedwait(void)
{
	sigset_t set;
	siginfo_t si;
	struct timespec ts, zero = { 0, 0 };
	struct timeval t0, t1;
	long ms;
	int r;

	subtest = 4;

	block(SIGUSR1, &set);
	if (sigtimedwait(&set, &si, &zero) != -1 || errno != EAGAIN) e(1);

	ts.tv_sec = 0;
	ts.tv_nsec = 200000000;
	gettimeofday(&t0, NULL);
	r = sigtimedwait(&set, &si, &ts);
	gettimeofday(&t1, NULL);
	if (r != -1 || errno != EAGAIN) e(2);
	ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_usec - t0.tv_usec) / 1000;
	if (ms < 180 || ms > 5000) e(3);

	ts.tv_nsec = 1000000000;
	if (sigtimedwait(&set, &si, &ts) != -1 || errno != EINVAL) e(4);
	ts.tv_nsec = -1;
	if (sigtimedwait(&set, &si, &ts) != -1 || errno != EINVAL) e(5);

	/* A pending signal: taken at once, with where it came from, and the
	 * handler does not run. */
	handle(SIGUSR1, recorder);
	nrec = 0;
	if (kill(getpid(), SIGUSR1) != 0) e(6);
	memset(&si, 0, sizeof(si));
	if (sigwaitinfo(&set, &si) != SIGUSR1) e(7);
	if (si.si_signo != SIGUSR1 || si.si_code != SI_USER ||
	    si.si_pid != getpid() || si.si_uid != getuid()) e(8);
	if (sigpending(&set) != 0 || sigismember(&set, SIGUSR1)) e(9);
	if (nrec != 0) e(10);

	/* No siginfo_t wanted. */
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	if (kill(getpid(), SIGUSR1) != 0) e(11);
	if (sigwaitinfo(&set, NULL) != SIGUSR1) e(12);

	dispose(SIGUSR1, SIG_DFL);
	unblock_all();
}

/* Waiting: a signal from another process ends the wait, and a caught
 * signal outside the set interrupts it. */
static void
test_wait_other(void)
{
	sigset_t set, set2;
	siginfo_t si;
	pid_t pid, parent = getpid();
	int sig, status;

	subtest = 5;

	block(SIGRTMIN + 2, &set);
	if ((pid = fork()) == 0) {
		usleep(100000);
		queue(parent, SIGRTMIN + 2, 77);
		_exit(0);
	}
	if (pid < 0) e(1);
	memset(&si, 0, sizeof(si));
	if (sigwaitinfo(&set, &si) != SIGRTMIN + 2) e(2);
	if (si.si_value.sival_int != 77 || si.si_pid != pid ||
	    si.si_code != SI_QUEUE) e(3);
	if (waitpid(pid, &status, 0) != pid || status != 0) e(4);

	/* sigwait() too. */
	if ((pid = fork()) == 0) {
		usleep(100000);
		kill(parent, SIGRTMIN + 2);
		_exit(0);
	}
	if (pid < 0) e(5);
	if (sigwait(&set, &sig) != 0 || sig != SIGRTMIN + 2) e(6);
	if (waitpid(pid, &status, 0) != pid || status != 0) e(7);

	/* A caught signal, not in the set, interrupts the wait. */
	dispose(SIGUSR1, catcher);
	if ((pid = fork()) == 0) {
		usleep(100000);
		kill(parent, SIGUSR1);
		_exit(0);
	}
	if (pid < 0) e(8);
	if (sigwaitinfo(&set, &si) != -1 || errno != EINTR) e(9);
	if (waitpid(pid, &status, 0) != pid || status != 0) e(10);
	/* And then the waited-for signal is still taken later. */
	if (queue(getpid(), SIGRTMIN + 2, 5) != 0) e(11);
	if (sigwaitinfo(&set, &si) != SIGRTMIN + 2 ||
	    si.si_value.sival_int != 5) e(12);
	/* sigwait() is not interrupted (POSIX has no EINTR for it): the
	 * handler runs, and it goes on waiting. */
	sigemptyset(&set2);
	sigaddset(&set2, SIGRTMIN + 2);
	caught = 0;
	if ((pid = fork()) == 0) {
		usleep(100000);
		kill(parent, SIGUSR1);
		usleep(100000);
		kill(parent, SIGRTMIN + 2);
		_exit(0);
	}
	if (pid < 0) e(13);
	errno = 0;
	if (sigwait(&set2, &sig) != 0 || sig != SIGRTMIN + 2) e(14);
	if (caught != SIGUSR1 || errno != 0) e(15);
	if (waitpid(pid, &status, 0) != pid || status != 0) e(16);

	dispose(SIGUSR1, SIG_DFL);
	unblock_all();
}

static volatile int thread_sig;
static volatile int thread_pending;

static void *
waiter(void *arg)
{
	sigset_t set;
	int sig;

	(void)arg;
	sigemptyset(&set);
	sigaddset(&set, SIGUSR2);
	if (sigwait(&set, &sig) == 0)
		thread_sig = sig;
	return NULL;
}

static void *
pending_check(void *arg)
{
	sigset_t set;

	(void)arg;
	if (sigpending(&set) == 0 && !sigismember(&set, SIGUSR1))
		thread_pending = 1;
	return NULL;
}

/* A thread in sigwait() takes a signal sent to the process; a new thread
 * and a child of fork() have none pending. */
static void
test_threads_fork(void)
{
	sigset_t set, pend;
	pthread_t t;
	pid_t pid;
	int status;

	subtest = 6;

	sigemptyset(&set);
	sigaddset(&set, SIGUSR2);
	if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0) e(1);
	thread_sig = 0;
	if (pthread_create(&t, NULL, waiter, NULL) != 0) e(2);
	usleep(100000);
	if (kill(getpid(), SIGUSR2) != 0) e(3);
	if (pthread_join(t, NULL) != 0) e(4);
	if (thread_sig != SIGUSR2) e(5);
	if (sigpending(&pend) != 0 || sigismember(&pend, SIGUSR2)) e(6);

	/* A pending signal stays with the creating thread... */
	block(SIGUSR1, &set);
	if (pthread_kill(pthread_self(), SIGUSR1) != 0) e(7);
	thread_pending = 0;
	if (pthread_create(&t, NULL, pending_check, NULL) != 0) e(8);
	if (pthread_join(t, NULL) != 0) e(9);
	if (!thread_pending) e(10);

	/* ...and with the parent. */
	if ((pid = fork()) == 0) {
		if (sigpending(&pend) != 0 || sigismember(&pend, SIGUSR1))
			_exit(1);
		_exit(0);
	}
	if (pid < 0) e(11);
	if (waitpid(pid, &status, 0) != pid || status != 0) e(12);

	if (sigwaitinfo(&set, NULL) != SIGUSR1) e(13);
	unblock_all();
}

static volatile int raised_pending;

static void *
raiser(void *arg)
{
	sigset_t set;

	(void)arg;
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0)
		return NULL;
	if (raise(SIGUSR1) != 0)
		return NULL;
	if (sigpending(&set) == 0 && sigismember(&set, SIGUSR1))
		raised_pending = 1;
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	(void)sigwaitinfo(&set, NULL);
	return NULL;
}

static void
test_misc(void)
{
	sigset_t set;
	stack_t ss;
	pthread_t t;
	char *stk;

	subtest = 7;

	/* raise() in a thread that blocks the signal: pending for that
	 * thread, not taken by the main thread, which does not block it. */
	nrec = 0;
	handle(SIGUSR1, recorder);
	raised_pending = 0;
	if (pthread_create(&t, NULL, raiser, NULL) != 0) e(1);
	if (pthread_join(t, NULL) != 0) e(2);
	if (!raised_pending || nrec != 0) e(3);
	dispose(SIGUSR1, SIG_DFL);

	if (raise(_NSIG) != -1 || errno != EINVAL) e(4);

	/* This program has the threads library. */
	sigemptyset(&set);
	if (sigprocmask(0, &set, NULL) != -1 || errno != EINVAL) e(5);
	if (sigprocmask(SIG_BLOCK + SIG_UNBLOCK + SIG_SETMASK + 1, &set,
	    NULL) != -1 || errno != EINVAL) e(6);
	if (pthread_sigmask(0, &set, NULL) != EINVAL) e(7);

	if ((stk = malloc(SIGSTKSZ)) == NULL) e(8);
	ss.ss_sp = stk;
	ss.ss_size = SIGSTKSZ;
	ss.ss_flags = SS_DISABLE | SS_ONSTACK;
	if (sigaltstack(&ss, NULL) != -1 || errno != EINVAL) e(9);
	ss.ss_flags = SS_ONSTACK;	/* ignored, as on NetBSD and Linux */
	if (sigaltstack(&ss, NULL) != 0) e(10);
	ss.ss_flags = SS_DISABLE;
	if (sigaltstack(&ss, NULL) != 0) e(11);
	free(stk);
}

static void *
timed_waiter(void *arg)
{
	struct timespec ts = { 5, 0 };
	sigset_t set;

	(void)arg;
	sigemptyset(&set);
	sigaddset(&set, SIGUSR2);
	(void)sigtimedwait(&set, NULL, &ts);
	return NULL;
}

/* A process exits while a thread of it is in sigtimedwait() with a timeout:
 * the thread's timer must go with it, or the next process in its slot
 * copies over a queued timer and PM crashes when it goes off.  PM hands out
 * slots in turn, so go round the whole table before the timeout. */
static void
test_exit_timedwait(void)
{
	sigset_t set;
	pthread_t t;
	pid_t pid;
	int i, status;

	subtest = 8;

	if ((pid = fork()) == 0) {
		block(SIGUSR2, &set);
		if (pthread_create(&t, NULL, timed_waiter, NULL) != 0)
			_exit(1);
		usleep(100000);
		_exit(0);
	}
	if (pid < 0) e(1);
	if (waitpid(pid, &status, 0) != pid || status != 0) e(2);

	for (i = 0; i < 600; i++) {
		if ((pid = fork()) == 0)
			_exit(0);
		if (pid < 0) e(3);
		if (waitpid(pid, &status, 0) != pid || status != 0) e(4);
	}
	sleep(6);			/* the timeout has passed */
	if ((pid = fork()) == 0)
		_exit(0);
	if (pid < 0) e(5);
	if (waitpid(pid, &status, 0) != pid || status != 0) e(6);
}

int
main(int argc, char **argv)
{
	start(146);

	test_sysconf();
	test_queue_handler();
	test_queue_limit();
	test_timedwait();
	test_wait_other();
	test_threads_fork();
	test_misc();
	test_exit_timedwait();

	quit();
	return 0;
}
