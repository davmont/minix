/* Test 163 - process-shared pthread objects (PTHREAD_PROCESS_SHARED).
 *
 * Mutexes, condition variables, rwlocks and barriers initialized as
 * process-shared, in a file mapping that a parent and its children share,
 * work across the processes: a mutex excludes, and its owner is known for
 * PTHREAD_MUTEX_ERRORCHECK; a signal or broadcast wakes a waiter in another
 * process; readers share an rwlock that keeps writers out; a barrier lets
 * all go at once.  Their attributes say process-shared once set so, and the
 * objects can be made and destroyed over and over.
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

int max_error = 4;

#define CHILDREN	4
#define INCS		2000

struct shared {
	pthread_mutex_t	mutex;
	pthread_mutex_t	emutex;		/* PTHREAD_MUTEX_ERRORCHECK */
	pthread_cond_t	cond;
	pthread_rwlock_t rwlock;
	pthread_barrier_t barrier;
	volatile int	count;
	volatile int	flag;
	volatile int	ready;
	volatile int	serial;
	volatile int	readers;
	volatile int	result;
};

static struct shared *sh;

static void
sleep_ms(int ms)
{
	struct timespec ts;

	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (ms % 1000) * 1000000L;
	while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
		;
}

/* An absolute timeout 'ms' from now on 'clock'. */
static struct timespec
after(clockid_t clock, int ms)
{
	struct timespec ts;

	clock_gettime(clock, &ts);
	ts.tv_sec += ms / 1000;
	ts.tv_nsec += (ms % 1000) * 1000000L;
	if (ts.tv_nsec >= 1000000000L) {
		ts.tv_sec++;
		ts.tv_nsec -= 1000000000L;
	}
	return ts;
}

/* The children exit with their own count of errors. */
static void
wait_children(int n, int err)
{
	int status;

	while (n-- > 0) {
		if (wait(&status) < 0) e(err);
		else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
			e(err + 1);
	}
}

static pid_t
child(void)
{
	pid_t pid;

	if ((pid = fork()) == 0)
		errct = 0;
	if (pid < 0) e(90);
	return pid;
}

static void
map_shared(void)
{
	int fd;

	if ((fd = open("shared", O_RDWR | O_CREAT | O_TRUNC, 0600)) < 0) e(91);
	if (ftruncate(fd, sizeof(*sh)) != 0) e(92);
	sh = mmap(NULL, sizeof(*sh), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (sh == MAP_FAILED) e(93);
	close(fd);
}

static void
test163a(void)
{
	pthread_mutexattr_t ma;
	pthread_condattr_t ca;
	pthread_rwlockattr_t ra;
	pthread_barrierattr_t ba;
	clockid_t clock;
	int ps;

	subtest = 1;

	if (sysconf(_SC_THREAD_PROCESS_SHARED) <= 0) e(1);

	if (pthread_mutexattr_init(&ma) != 0) e(2);
	if (pthread_mutexattr_getpshared(&ma, &ps) != 0) e(3);
	if (ps != PTHREAD_PROCESS_PRIVATE) e(4);
	if (pthread_mutexattr_setpshared(&ma, PTHREAD_PROCESS_SHARED) != 0) e(5);
	if (pthread_mutexattr_getpshared(&ma, &ps) != 0) e(6);
	if (ps != PTHREAD_PROCESS_SHARED) e(7);
	if (pthread_mutexattr_setpshared(&ma, 42) != EINVAL) e(8);
	/* The type is kept apart. */
	if (pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_RECURSIVE) != 0) e(9);
	if (pthread_mutexattr_getpshared(&ma, &ps) != 0) e(10);
	if (ps != PTHREAD_PROCESS_SHARED) e(11);
	pthread_mutexattr_destroy(&ma);

	if (pthread_condattr_init(&ca) != 0) e(12);
	if (pthread_condattr_getclock(&ca, &clock) != 0) e(13);
	if (clock != CLOCK_REALTIME) e(14);	/* the default */
	if (pthread_condattr_setpshared(&ca, PTHREAD_PROCESS_SHARED) != 0)
		e(15);
	if (pthread_condattr_setclock(&ca, CLOCK_MONOTONIC) != 0) e(16);
	if (pthread_condattr_getpshared(&ca, &ps) != 0) e(17);
	if (ps != PTHREAD_PROCESS_SHARED) e(18);
	if (pthread_condattr_getclock(&ca, &clock) != 0) e(19);
	if (clock != CLOCK_MONOTONIC) e(20);
	if (pthread_condattr_setpshared(&ca, -1) != EINVAL) e(21);
	pthread_condattr_destroy(&ca);

	if (pthread_rwlockattr_init(&ra) != 0) e(22);
	if (pthread_rwlockattr_getpshared(&ra, &ps) != 0) e(23);
	if (ps != PTHREAD_PROCESS_PRIVATE) e(24);
	if (pthread_rwlockattr_setpshared(&ra, PTHREAD_PROCESS_SHARED) != 0)
		e(25);
	if (pthread_rwlockattr_getpshared(&ra, &ps) != 0) e(26);
	if (ps != PTHREAD_PROCESS_SHARED) e(27);
	pthread_rwlockattr_destroy(&ra);

	if (pthread_barrierattr_init(&ba) != 0) e(28);
	if (pthread_barrierattr_getpshared(&ba, &ps) != 0) e(29);
	if (ps != PTHREAD_PROCESS_PRIVATE) e(30);
	if (pthread_barrierattr_setpshared(&ba, PTHREAD_PROCESS_SHARED) != 0)
		e(31);
	if (pthread_barrierattr_getpshared(&ba, &ps) != 0) e(32);
	if (ps != PTHREAD_PROCESS_SHARED) e(33);
	pthread_barrierattr_destroy(&ba);
}

/* Mutexes: exclusion across processes, and an errorcheck one's owner. */
static void
test163b(void)
{
	pthread_mutexattr_t ma;
	struct timespec ts;
	int i, n;

	subtest = 2;

	pthread_mutexattr_init(&ma);
	pthread_mutexattr_setpshared(&ma, PTHREAD_PROCESS_SHARED);
	if (pthread_mutex_init(&sh->mutex, &ma) != 0) e(1);
	pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_ERRORCHECK);
	if (pthread_mutex_init(&sh->emutex, &ma) != 0) e(2);
	pthread_mutexattr_destroy(&ma);

	/* Increments that are not atomic: lost ones show a lock that does not
	 * exclude. */
	sh->count = 0;
	for (n = 0; n < CHILDREN; n++) {
		if (child() == 0) {
			for (i = 0; i < INCS; i++) {
				if (pthread_mutex_lock(&sh->mutex) != 0) e(3);
				sh->count = sh->count + 1;
				if ((i & 63) == 0)
					sched_yield();
				if (pthread_mutex_unlock(&sh->mutex) != 0)
					e(4);
			}
			exit(errct);
		}
	}
	wait_children(CHILDREN, 5);
	if (sh->count != CHILDREN * INCS) e(7);

	/* Held here: another process cannot have it, nor let it go. */
	if (pthread_mutex_lock(&sh->emutex) != 0) e(8);
	if (pthread_mutex_lock(&sh->emutex) != EDEADLK) e(9);
	if (child() == 0) {
		if (pthread_mutex_trylock(&sh->emutex) != EBUSY) e(10);
		if (pthread_mutex_unlock(&sh->emutex) != EPERM) e(11);
		ts = after(CLOCK_REALTIME, 200);
		if (pthread_mutex_timedlock(&sh->emutex, &ts) != ETIMEDOUT)
			e(12);
		/* Blocks until the parent lets it go. */
		sh->ready = 1;
		if (pthread_mutex_lock(&sh->emutex) != 0) e(13);
		if (sh->flag != 1) e(14);
		if (pthread_mutex_unlock(&sh->emutex) != 0) e(15);
		exit(errct);
	}
	while (!sh->ready)
		sleep_ms(1);
	sleep_ms(100);
	if (pthread_mutex_destroy(&sh->emutex) != EBUSY) e(16);
	sh->flag = 1;
	if (pthread_mutex_unlock(&sh->emutex) != 0) e(17);
	wait_children(1, 18);
	if (pthread_mutex_unlock(&sh->emutex) != EPERM) e(20);

	if (pthread_mutex_destroy(&sh->emutex) != 0) e(21);
	if (pthread_mutex_destroy(&sh->mutex) != 0) e(22);
}

/* Condition variables: a signal and a broadcast across processes, and a
 * timeout on CLOCK_MONOTONIC. */
static void
test163c(void)
{
	pthread_mutexattr_t ma;
	pthread_condattr_t ca;
	struct timespec ts;
	int n, r;

	subtest = 3;

	pthread_mutexattr_init(&ma);
	pthread_mutexattr_setpshared(&ma, PTHREAD_PROCESS_SHARED);
	if (pthread_mutex_init(&sh->mutex, &ma) != 0) e(1);
	pthread_mutexattr_destroy(&ma);
	pthread_condattr_init(&ca);
	pthread_condattr_setpshared(&ca, PTHREAD_PROCESS_SHARED);
	pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
	if (pthread_cond_init(&sh->cond, &ca) != 0) e(2);
	pthread_condattr_destroy(&ca);

	/* The child sets the flag and signals; we wait for it. */
	sh->flag = 0;
	if (child() == 0) {
		sleep_ms(50);
		pthread_mutex_lock(&sh->mutex);
		sh->flag = 1;
		if (pthread_cond_signal(&sh->cond) != 0) e(3);
		pthread_mutex_unlock(&sh->mutex);
		exit(errct);
	}
	pthread_mutex_lock(&sh->mutex);
	while (sh->flag == 0)
		if (pthread_cond_wait(&sh->cond, &sh->mutex) != 0) e(4);
	pthread_mutex_unlock(&sh->mutex);
	wait_children(1, 5);

	/* All the children wait; one broadcast wakes all. */
	sh->flag = 0;
	sh->ready = 0;
	for (n = 0; n < CHILDREN; n++) {
		if (child() == 0) {
			pthread_mutex_lock(&sh->mutex);
			sh->ready++;
			while (sh->flag == 0)
				if (pthread_cond_wait(&sh->cond, &sh->mutex)
				    != 0)
					e(7);
			pthread_mutex_unlock(&sh->mutex);
			exit(errct);
		}
	}
	for (;;) {
		pthread_mutex_lock(&sh->mutex);
		n = sh->ready;
		pthread_mutex_unlock(&sh->mutex);
		if (n == CHILDREN)
			break;
		sleep_ms(1);
	}
	sleep_ms(50);			/* into their waits */
	pthread_mutex_lock(&sh->mutex);
	sh->flag = 1;
	if (pthread_cond_broadcast(&sh->cond) != 0) e(8);
	pthread_mutex_unlock(&sh->mutex);
	wait_children(CHILDREN, 9);

	/* Nobody signals: the timeout on the condition's clock. */
	pthread_mutex_lock(&sh->mutex);
	ts = after(CLOCK_MONOTONIC, 100);
	r = pthread_cond_timedwait(&sh->cond, &sh->mutex, &ts);
	if (r != ETIMEDOUT) e(11);
	if (!pthread_mutex_trylock(&sh->mutex)) e(12);	/* still ours */
	pthread_mutex_unlock(&sh->mutex);
	/* Not holding the mutex. */
	if (pthread_cond_wait(&sh->cond, &sh->mutex) != EPERM) e(13);

	if (pthread_cond_destroy(&sh->cond) != 0) e(14);
	if (pthread_mutex_destroy(&sh->mutex) != 0) e(15);
}

/* Rwlocks: readers in two processes at once, writers kept out. */
static void
test163d(void)
{
	pthread_rwlockattr_t ra;
	struct timespec ts;

	subtest = 4;

	pthread_rwlockattr_init(&ra);
	pthread_rwlockattr_setpshared(&ra, PTHREAD_PROCESS_SHARED);
	if (pthread_rwlock_init(&sh->rwlock, &ra) != 0) e(1);
	pthread_rwlockattr_destroy(&ra);

	if (pthread_rwlock_rdlock(&sh->rwlock) != 0) e(2);
	sh->ready = 0;
	sh->flag = 0;
	if (child() == 0) {
		/* Another reader gets in; a writer does not. */
		if (pthread_rwlock_tryrdlock(&sh->rwlock) != 0) e(3);
		if (pthread_rwlock_unlock(&sh->rwlock) != 0) e(4);
		if (pthread_rwlock_trywrlock(&sh->rwlock) != EBUSY) e(5);
		ts = after(CLOCK_REALTIME, 100);
		if (pthread_rwlock_timedwrlock(&sh->rwlock, &ts) != ETIMEDOUT)
			e(6);
		/* Blocks until the parent's read lock goes. */
		sh->ready = 1;
		if (pthread_rwlock_wrlock(&sh->rwlock) != 0) e(7);
		if (sh->flag != 1) e(8);
		sh->flag = 2;
		sleep_ms(100);
		if (pthread_rwlock_unlock(&sh->rwlock) != 0) e(9);
		exit(errct);
	}
	while (!sh->ready)
		sleep_ms(1);
	sleep_ms(50);
	sh->flag = 1;
	if (pthread_rwlock_unlock(&sh->rwlock) != 0) e(10);
	/* The writer has it now, or will before us. */
	while (sh->flag != 2)
		sleep_ms(1);
	if (pthread_rwlock_tryrdlock(&sh->rwlock) != EBUSY) e(11);
	if (pthread_rwlock_rdlock(&sh->rwlock) != 0) e(12);
	if (pthread_rwlock_unlock(&sh->rwlock) != 0) e(13);
	wait_children(1, 14);
	if (pthread_rwlock_unlock(&sh->rwlock) != EPERM) e(16);

	if (pthread_rwlock_destroy(&sh->rwlock) != 0) e(17);
}

/* Barriers: rounds of all the processes, one serial thread each. */
static void
test163e(void)
{
	pthread_barrierattr_t ba;
	int n, i, r;

#define ROUNDS	20
	subtest = 5;

	pthread_barrierattr_init(&ba);
	pthread_barrierattr_setpshared(&ba, PTHREAD_PROCESS_SHARED);
	if (pthread_barrier_init(&sh->barrier, &ba, CHILDREN + 1) != 0) e(1);
	pthread_barrierattr_destroy(&ba);
	sh->serial = 0;
	sh->count = 0;

	for (n = 0; n < CHILDREN; n++) {
		if (child() == 0) {
			for (i = 0; i < ROUNDS; i++) {
				__atomic_fetch_add(&sh->count, 1,
				    __ATOMIC_SEQ_CST);
				r = pthread_barrier_wait(&sh->barrier);
				if (r == PTHREAD_BARRIER_SERIAL_THREAD)
					__atomic_fetch_add(&sh->serial, 1,
					    __ATOMIC_SEQ_CST);
				else if (r != 0) e(2);
				/* All have arrived in this round. */
				if (sh->count < (i + 1) * (CHILDREN + 1)) e(3);
			}
			exit(errct);
		}
	}
	for (i = 0; i < ROUNDS; i++) {
		__atomic_fetch_add(&sh->count, 1, __ATOMIC_SEQ_CST);
		r = pthread_barrier_wait(&sh->barrier);
		if (r == PTHREAD_BARRIER_SERIAL_THREAD)
			__atomic_fetch_add(&sh->serial, 1, __ATOMIC_SEQ_CST);
		else if (r != 0) e(4);
	}
	wait_children(CHILDREN, 5);
	if (sh->serial != ROUNDS) e(7);
	if (pthread_barrier_destroy(&sh->barrier) != 0) e(8);
}

static volatile int caught;

static void
catch(int sig)
{
	caught = sig;
}

static void *
locker(void *arg)
{
	int i;

	for (i = 0; i < INCS; i++) {
		if (pthread_mutex_lock(&sh->mutex) != 0) e(1);
		sh->count = sh->count + 1;
		if (pthread_mutex_unlock(&sh->mutex) != 0) e(2);
	}
	return arg;
}

/* Threads of one process on a shared mutex, a signal to a thread blocked on
 * it, and many objects made and destroyed. */
static void
test163f(void)
{
	pthread_mutexattr_t ma;
	pthread_condattr_t ca;
	pthread_t t[3];
	pthread_mutex_t m;
	pthread_cond_t c;
	pid_t pid;
	int i;

	subtest = 6;

	pthread_mutexattr_init(&ma);
	pthread_mutexattr_setpshared(&ma, PTHREAD_PROCESS_SHARED);
	if (pthread_mutex_init(&sh->mutex, &ma) != 0) e(3);

	sh->count = 0;
	for (i = 0; i < 3; i++)
		if (pthread_create(&t[i], NULL, locker, NULL) != 0) e(4);
	for (i = 0; i < 3; i++)
		if (pthread_join(t[i], NULL) != 0) e(5);
	if (sh->count != 3 * INCS) e(6);

	/* A signal does not end a wait for a mutex: the handler runs, and the
	 * thread goes on waiting until it has the mutex. */
	if (pthread_mutex_lock(&sh->mutex) != 0) e(7);
	sh->ready = 0;
	if ((pid = child()) == 0) {
		signal(SIGUSR1, catch);
		sh->ready = 1;
		if (pthread_mutex_lock(&sh->mutex) != 0) e(8);
		if (caught != SIGUSR1) e(9);
		if (sh->flag != 3) e(10);
		pthread_mutex_unlock(&sh->mutex);
		exit(errct);
	}
	while (!sh->ready)
		sleep_ms(1);
	sleep_ms(100);			/* into its wait */
	if (kill(pid, SIGUSR1) != 0) e(11);
	sleep_ms(100);
	sh->flag = 3;
	if (pthread_mutex_unlock(&sh->mutex) != 0) e(12);
	wait_children(1, 13);
	if (pthread_mutex_destroy(&sh->mutex) != 0) e(15);

	/* Their ids come back: more than the server holds at once. */
	pthread_condattr_init(&ca);
	pthread_condattr_setpshared(&ca, PTHREAD_PROCESS_SHARED);
	for (i = 0; i < 10000; i++) {
		if (pthread_mutex_init(&m, &ma) != 0) { e(16); break; }
		if (pthread_cond_init(&c, &ca) != 0) { e(17); break; }
		if (pthread_mutex_lock(&m) != 0) e(18);
		if (pthread_cond_signal(&c) != 0) e(19);
		if (pthread_mutex_unlock(&m) != 0) e(20);
		if (pthread_cond_destroy(&c) != 0) e(21);
		if (pthread_mutex_destroy(&m) != 0) e(22);
	}
	pthread_condattr_destroy(&ca);
	pthread_mutexattr_destroy(&ma);
}

int
main(int argc, char **argv)
{
	start(163);

	map_shared();
	test163a();
	test163b();
	test163c();
	test163d();
	test163e();
	test163f();

	quit();
	return 0;
}
