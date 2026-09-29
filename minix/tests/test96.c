/* Test 96 - stack guard pages.
 *
 * A thread that overflows its stack must hit the guard page libpthread puts
 * below it (with mprotect(PROT_NONE)) and die of SIGSEGV, instead of running
 * on into whatever memory lies below.  The same for the main thread's stack.
 * Each overflow runs in a child process; the parent checks how it ended.
 */
#include <sys/wait.h>
#include <pthread.h>
#include <signal.h>

#include "common.h"

int max_error = 0;

static volatile int sink;

/* Recurse until the stack runs out; each frame keeps 1 KB live. */
static int
recurse(int depth)
{
	volatile char frame[1024];

	frame[0] = (char)depth;
	frame[sizeof(frame) - 1] = (char)depth;
	return recurse(depth + 1) + frame[0] + frame[sizeof(frame) - 1];
}

static void *
overflow_thread(void *arg)
{
	sink = recurse(0);
	return arg;
}

/* Run fn in a child; return 1 if the child died of SIGSEGV. */
static int
died_of_segv(void (*fn)(void))
{
	int status;
	pid_t pid;

	pid = fork();
	if (pid == 0) {
		fn();
		_exit(0);	/* not reached */
	}
	if (pid < 0) {
		e(100);
		return 0;
	}
	if (waitpid(pid, &status, 0) != pid) {
		e(101);
		return 0;
	}
	return WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV;
}

static void
thread_overflow(void)
{
	pthread_attr_t attr;
	pthread_t t;

	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 64 * 1024);
	if (pthread_create(&t, &attr, overflow_thread, NULL) != 0)
		_exit(2);
	pthread_join(t, NULL);
}

static void
main_overflow(void)
{
	sink = recurse(0);
}

int
main(int argc, char **argv)
{
	start(96);

	/* The dumps of the dying children are of no interest. */
	signal(SIGSEGV, SIG_DFL);

	subtest = 1;
	if (!died_of_segv(thread_overflow)) e(1);

	subtest = 2;
	if (!died_of_segv(main_overflow)) e(2);

	quit();
	return 0;
}
