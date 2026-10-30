/* Test 164 - mlock(2) and co., PTHREAD_STACK_MIN, SIGPOLL.
 *
 * The memory locking calls check what POSIX has them check: only the
 * superuser may lock, the range must start on a page and be mapped, and
 * mlockall() takes MCL_CURRENT and MCL_FUTURE only.  A thread runs, and
 * takes a signal, on a stack of PTHREAD_STACK_MIN bytes, which is what
 * sysconf(_SC_THREAD_STACK_MIN) says.
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

static void
test164a(void)
{
	long pg = sysconf(_SC_PAGESIZE);
	char *p;
	pid_t pid;
	int status;

	subtest = 1;

	p = mmap(NULL, 4 * pg, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE,
	    -1, 0);
	if (p == MAP_FAILED) e(1);

	if (mlock(p, 4 * pg) != 0) e(2);
	if (munlock(p, 4 * pg) != 0) e(3);
	if (mlock(p + 1, pg) != -1 || errno != EINVAL) e(4);
	if (munlock(p + 1, pg) != -1 || errno != EINVAL) e(5);
	/* Not mapped: part of the range, and all of it. */
	if (munmap(p + 2 * pg, 2 * pg) != 0) e(6);
	if (mlock(p, 4 * pg) != -1 || errno != ENOMEM) e(7);
	if (mlock(p + 2 * pg, pg) != -1 || errno != ENOMEM) e(8);
	if (munlock(p + 2 * pg, pg) != -1 || errno != ENOMEM) e(9);

	if (mlockall(MCL_CURRENT) != 0) e(10);
	if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) e(11);
	if (mlockall(0) != -1 || errno != EINVAL) e(12);
	if (mlockall(0x100) != -1 || errno != EINVAL) e(13);
	if (munlockall() != 0) e(14);

	/* An ordinary user may not lock. */
	if ((pid = fork()) == 0) {
		errct = 0;
		if (setuid(1) != 0) e(15);
		if (mlock(p, pg) != -1 || errno != EPERM) e(16);
		if (mlockall(MCL_CURRENT) != -1 || errno != EPERM) e(17);
		if (munlock(p, pg) != 0) e(18);
		if (munlockall() != 0) e(19);
		exit(errct);
	}
	if (pid < 0) e(20);
	if (waitpid(pid, &status, 0) != pid) e(21);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) e(22);

	munmap(p, 2 * pg);
}

static volatile int caught;

static void
catch(int sig)
{
	caught = sig;
}

static void *
small(void *arg)
{
	pthread_kill(pthread_self(), SIGUSR1);
	return (caught == SIGUSR1) ? arg : NULL;
}

static void
test164b(void)
{
	pthread_attr_t attr;
	pthread_t t;
	size_t size;
	void *stack, *res;

	subtest = 2;

	if (sysconf(_SC_THREAD_STACK_MIN) != PTHREAD_STACK_MIN) e(1);

	if (pthread_attr_init(&attr) != 0) e(2);
	if (pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN - 1) != EINVAL)
		e(3);
	if (pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN) != 0) e(4);
	if (pthread_attr_getstacksize(&attr, &size) != 0) e(5);
	if (size != PTHREAD_STACK_MIN) e(6);

	signal(SIGUSR1, catch);
	caught = 0;
	if (pthread_create(&t, &attr, small, &attr) != 0) e(7);
	if (pthread_join(t, &res) != 0) e(8);
	if (res != &attr) e(9);

	/* A stack of its own, as small. */
	if ((stack = malloc(PTHREAD_STACK_MIN)) == NULL) e(10);
	if (pthread_attr_setstack(&attr, stack, PTHREAD_STACK_MIN - 1) != EINVAL)
		e(16);
	if (pthread_attr_setstack(&attr, stack, PTHREAD_STACK_MIN) != 0) e(11);
	caught = 0;
	if (pthread_create(&t, &attr, small, &attr) != 0) e(12);
	if (pthread_join(t, &res) != 0) e(13);
	if (res != &attr) e(14);
	pthread_attr_destroy(&attr);
	free(stack);
	signal(SIGUSR1, SIG_DFL);

	if (SIGPOLL != SIGIO) e(15);
}

int
main(int argc, char **argv)
{
	start(164);

	test164a();
	test164b();

	quit();
	return 0;
}
