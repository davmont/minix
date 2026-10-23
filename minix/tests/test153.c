/* Test 153 - a threaded process that exits while it takes signals.
 *
 * exit() in a threaded process tears down the other threads first, and the
 * main thread exits once they are gone.  A signal that came in meanwhile was
 * still delivered: to the main thread, waiting in exit(), PM sent VFS an
 * unpause, and when the last thread was gone the main thread was still
 * waiting for VFS.  PM panicked ("tell_vfs: not idle"), taking the system
 * down.
 *
 * Here a thread of each child sends its process SIGUSR1 without pause while
 * the main thread creates threads, joinable and detached, and then exit() is
 * called, by the main thread or by another one.  If PM dies, the test does
 * not finish.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

#define ROUNDS		200
#define THREADS		40

static sem_t sem;

static void *
sender(void *arg)
{
	struct timespec ts = { 0, 10000 };
	pid_t pid = getpid();
	sigset_t set;
	long n;

	(void)arg;
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	pthread_sigmask(SIG_BLOCK, &set, NULL);
	for (n = 0; ; n++) {
		if (n >= 100)
			nanosleep(&ts, NULL);
		kill(pid, SIGUSR1);
	}
	/* NOTREACHED */
	return NULL;
}

static void
handler(int sig)
{
	(void)sig;
}

static void *
poster(void *arg)
{
	while (sem_post(&sem) == -1 && errno == EINTR)
		;
	return arg;
}

static void *
exiter(void *arg)
{
	(void)arg;
	exit(0);
	/* NOTREACHED */
	return NULL;
}

static void
child(int by_thread)
{
	struct sigaction sa;
	pthread_attr_t detached;
	pthread_t t;
	int i;

	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = handler;
	if (sigaction(SIGUSR1, &sa, NULL) != 0) _exit(1);
	if (sem_init(&sem, 0, 0) != 0) _exit(2);
	pthread_attr_init(&detached);
	pthread_attr_setdetachstate(&detached, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&t, &detached, sender, NULL) != 0) _exit(3);

	for (i = 0; i < THREADS; i++) {
		if (pthread_create(&t, (i & 1) ? &detached : NULL, poster,
		    NULL) != 0)
			continue;
		while (sem_wait(&sem) == -1 && errno == EINTR)
			;
		if (!(i & 1))
			pthread_join(t, NULL);
	}

	if (by_thread) {
		if (pthread_create(&t, NULL, exiter, NULL) != 0) _exit(4);
		for (;;)
			pause();
	}
	exit(0);
}

int
main(int argc, char **argv)
{
	pid_t pid;
	int i, status;

	start(153);

	for (i = 0; i < ROUNDS; i++) {
		if ((pid = fork()) == 0)
			child(i & 1);
		if (pid < 0) { e(1); break; }
		if (waitpid(pid, &status, 0) != pid) e(2);
		else if (!WIFEXITED(status)) e(3);
		else if (WEXITSTATUS(status) != 0) e(4);
	}

	quit();
	return 0;
}
