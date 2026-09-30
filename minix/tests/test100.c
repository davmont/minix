/* Test 100 - execve() and exit of a threaded process.
 *
 * POSIX: a successful execve() by one thread ends every other thread of the
 * process, whatever they are doing; a failed execve() leaves them alone.
 * Also, a thread that exited without being joined must not keep a dying
 * process from finishing.  Each case runs in a child process; the parent
 * checks how it ended, with a timeout so that a hang is a failure, not a
 * stuck test.
 */
#include <sys/mman.h>
#include <sys/wait.h>
#include <limits.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>

#include "common.h"

int max_error = 0;

#define EXEC_EXIT	42		/* exit status of the exec'd image */

static char self[PATH_MAX];		/* absolute path of this binary */
static volatile unsigned long spins;
static volatile int zombie_done;
static int pipefd[2];

/* Busy on a CPU, but pausing now and then: on MINIX a CPU-bound thread can
 * starve the thread that created it for seconds (scheduler), which is not
 * what this test is about. */
static void *
spinner(void *arg)
{
	for (;;) {
		if ((++spins & 0xfffff) == 0)
			usleep(1000);
	}
	return arg;
}

static void *
reader(void *arg)
{
	char c;

	(void) read(pipefd[0], &c, 1);	/* blocks in VFS: nobody writes */
	return arg;
}

static void *
sleeper(void *arg)
{
	for (;;)
		usleep(10000);
	return arg;
}

static void *
quitter(void *arg)
{
	zombie_done = 1;
	return arg;			/* joinable, never joined: a zombie */
}

static void
start_thread(void *(*fn)(void *))
{
	pthread_t t;

	if (pthread_create(&t, NULL, fn, NULL) != 0)
		_exit(90);
}

/* Leave one exited-but-unjoined thread behind. */
static void
make_zombie(void)
{
	start_thread(quitter);
	while (!zombie_done)
		usleep(1000);
	usleep(50000);			/* let it finish exiting */
}

static void
start_busy_threads(void)
{
	if (pipe(pipefd) != 0)
		_exit(91);
	start_thread(spinner);
	start_thread(reader);
	start_thread(sleeper);
	usleep(50000);			/* let them get going */
}

/* Run fn in a child; wait for it (at most 60 s) and return its status, or -1
 * if it hung (then it is killed). */
static int
run_child(void (*fn)(void))
{
	int status, i;
	pid_t pid, r;

	if ((pid = fork()) < 0) e(80);
	if (pid == 0) {
		fn();
		_exit(99);		/* fn must not return */
	}
	for (i = 0; i < 6000; i++) {
		r = waitpid(pid, &status, WNOHANG);
		if (r == pid)
			return status;
		if (r < 0) e(81);
		usleep(10000);
	}
	kill(pid, SIGKILL);
	(void) waitpid(pid, &status, 0);
	return -1;
}

static void
exec_self(void)
{
	execl(self, self, "exec-child", (char *)NULL);
}

/* The main thread execs while other threads spin, sleep and block in a
 * read(): the exec succeeds and the new image runs alone. */
static void
child_exec_busy(void)
{
	start_busy_threads();
	exec_self();
	_exit(1);			/* exec failed */
}

/* The same with a zombie thread around. */
static void
child_exec_zombie(void)
{
	make_zombie();
	start_busy_threads();
	exec_self();
	_exit(1);
}

/* A failed exec leaves the other threads running. */
static void
child_exec_fails(void)
{
	unsigned long before;

	start_busy_threads();
	if (execl("/nonexistent/test100", "x", (char *)NULL) != -1)
		_exit(2);
	if (errno != ENOENT)
		_exit(3);
	before = spins;
	usleep(100000);
	_exit(spins != before ? 0 : 4);	/* the spinner still runs */
}

/* A fatal signal ends the process even with a zombie thread around. */
static void
child_signal_zombie(void)
{
	make_zombie();
	start_busy_threads();
	kill(getpid(), SIGTERM);
	for (;;)
		pause();
}

static volatile int thread_exec_errno;

static void *
exec_from_thread(void *arg)
{
	execl(self, self, "exec-child", (char *)NULL);
	thread_exec_errno = errno;
	return arg;
}

/* Only the main thread may exec for now (the process identity lives in the
 * main thread's PM slot); another thread gets ENOTSUP, cleanly. */
static void
child_exec_from_thread(void)
{
	pthread_t t;

	if (pthread_create(&t, NULL, exec_from_thread, NULL) != 0)
		_exit(90);
	if (pthread_join(t, NULL) != 0)
		_exit(5);
	if (thread_exec_errno != ENOTSUP) {
		printf("test100: exec from a thread: errno %d\n",
		    thread_exec_errno);
		_exit(6);
	}
	_exit(0);
}

static void
test_exec(void)
{
	int s;

	subtest = 1;
	s = run_child(child_exec_busy);
	if (s == -1) e(1);
	else if (!WIFEXITED(s) || WEXITSTATUS(s) != EXEC_EXIT) e(2);

	subtest = 2;
	s = run_child(child_exec_zombie);
	if (s == -1) e(1);
	else if (!WIFEXITED(s) || WEXITSTATUS(s) != EXEC_EXIT) e(2);

	subtest = 3;
	s = run_child(child_exec_fails);
	if (s == -1) e(1);
	else if (!WIFEXITED(s) || WEXITSTATUS(s) != 0) e(2);

	subtest = 4;
	s = run_child(child_exec_from_thread);
	if (s == -1) e(1);
	else if (!WIFEXITED(s) || WEXITSTATUS(s) != 0) {
		printf("test100: subtest 4 status 0x%x\n", s);
		e(2);
	}
}

#define MAPLEN	(16 * 4096)

static void * volatile thread_map;
static sigjmp_buf segv_jb;

static void
segv_jump(int sig)
{
	siglongjmp(segv_jb, 1);
}
static volatile int thread_brk_ok;

static void *
mapper(void *arg)
{
	char *p, *b;
	int i;

	p = mmap(NULL, MAPLEN, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE,
	    -1, 0);
	if (p == MAP_FAILED)
		return arg;
	for (i = 0; i < MAPLEN; i += 4096)
		p[i] = (char)(i / 4096 + 1);	/* fault each page in */
	thread_map = p;

	if ((b = sbrk(4096)) != (char *)-1) {
		b[0] = 1;
		thread_brk_ok = (b[0] == 1);
	}
	return arg;
}

/* Memory a thread maps is the process's: the other threads can use it and
 * unmap it, and it outlives the thread.  brk works from a thread too. */
static void
child_thread_memory(void)
{
	pthread_t t;
	char *p;
	int i;

	if (pthread_create(&t, NULL, mapper, NULL) != 0)
		_exit(90);
	if (pthread_join(t, NULL) != 0)
		_exit(10);
	if ((p = thread_map) == NULL)
		_exit(11);			/* mmap failed in the thread */
	/* Churn memory: if the pages went away with the thread, they get
	 * reused and the content changes. */
	for (i = 0; i < 8; i++) {
		char *q = mmap(NULL, 1024 * 1024, PROT_READ | PROT_WRITE,
		    MAP_ANON | MAP_PRIVATE, -1, 0);
		if (q == MAP_FAILED)
			_exit(15);
		memset(q, 0xa5, 1024 * 1024);
	}
	for (i = 0; i < MAPLEN; i += 4096)
		if (p[i] != (char)(i / 4096 + 1))
			_exit(12);		/* content lost */
	if (munmap(p, MAPLEN) != 0)
		_exit(13);
	/* Unmapped for real: touching it now faults. */
	signal(SIGSEGV, segv_jump);
	if (sigsetjmp(segv_jb, 1) == 0) {
		(void) *(volatile char *)p;
		_exit(16);			/* still mapped */
	}
	if (!thread_brk_ok)
		_exit(14);
	_exit(0);
}

static void
test_thread_memory(void)
{
	int s;

	subtest = 6;
	s = run_child(child_thread_memory);
	if (s == -1) e(1);
	else if (!WIFEXITED(s) || WEXITSTATUS(s) != 0) {
		printf("test100: subtest 6 status 0x%x\n", s);
		e(2);
	}
}

static void
test_signal(void)
{
	int s;

	subtest = 5;
	s = run_child(child_signal_zombie);
	if (s == -1) e(1);
	else if (!WIFSIGNALED(s) || WTERMSIG(s) != SIGTERM) e(2);
}

int
main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "exec-child") == 0)
		exit(EXEC_EXIT);	/* we are the exec'd image */

	if (realpath(argv[0], self) == NULL) {
		printf("test100: cannot resolve %s\n", argv[0]);
		exit(1);
	}

	start(100);

	test_thread_memory();
	test_exec();
	test_signal();

	quit();
	return 0;
}
