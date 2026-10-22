/*
 * POSIX asynchronous I/O: aio_read(), aio_write(), aio_fsync(), aio_error(),
 * aio_return(), aio_cancel(), aio_suspend() and lio_listio().
 *
 * MINIX has no kernel AIO.  As in glibc and musl, a small pool of threads
 * does the I/O: each runs one request at a time with pread() or pwrite() (or
 * read() and write() where the file cannot seek) and so blocks only itself,
 * since every thread has its own process slot in VFS.  As in glibc, the
 * requests on one descriptor run one at a time, in the order they came:
 * writes with O_APPEND land in that order, as POSIX has it, and an
 * aio_fsync() follows the requests before it.  The workers block
 * every signal, so a signal meant for the program never lands on one, and
 * a SIGEV_SIGNAL notification goes to the process through PM as a sigqueue()
 * with si_code SI_ASYNCIO, which SIGQUEUE_MAX does not limit.
 *
 * A request's state lives in the aiocb's private fields.  aio_error() and
 * aio_return() are async-signal-safe (POSIX): a handler calls them, for the
 * request whose completion signal it is, and they take no lock but read and
 * move _state atomically, which publishes _errno and _retval.  The program's
 * threads hold aio_lock with every signal blocked, so that a handler calling
 * aio_suspend() does not find the lock taken by the thread it interrupted;
 * the workers block every signal anyway.
 *
 * aio_suspend() and lio_listio(LIO_WAIT) must return EINTR when a signal
 * arrives, which a condition variable cannot do: the waiter sleeps in poll()
 * on a pipe of its own instead, and each completion writes to the pipes of
 * the threads waiting.
 */

#include <sys/cdefs.h>
#include <lib.h>

#include <aio.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/queue.h>

/* Request states, in aiocb._state; a zeroed aiocb is AIO_NONE. */
#define AIO_NONE	0	/* never submitted */
#define AIO_QUEUED	1	/* waiting for a worker */
#define AIO_RUNNING	2	/* a worker is doing it */
#define AIO_DONE	3	/* complete, aio_return() not yet called */
#define AIO_RETURNED	4	/* aio_return() called */

#define OP_READ		1
#define OP_WRITE	2
#define OP_FSYNC	3
#define OP_FDATASYNC	4

/* AIO_MAX (<limits.h>) counts a request until aio_return() frees it, as
 * POSIX has it.  At most AIO_WORKERS requests run at once. */
#define AIO_WORKERS	16

struct lio;

struct job {
	TAILQ_ENTRY(job) j_link;	/* in aio_queue or aio_running */
	struct aiocb *j_cb;
	int j_op;
	struct lio *j_lio;		/* the lio_listio() it belongs to */
};

struct lio {
	int l_pending;			/* requests not done yet */
	int l_wait;			/* LIO_WAIT: the caller frees it */
	struct sigevent l_sig;		/* LIO_NOWAIT notification */
};

struct waiter {
	LIST_ENTRY(waiter) w_link;
	int w_fd;			/* write end of the waiter's pipe */
};

static pthread_mutex_t aio_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t aio_work = PTHREAD_COND_INITIALIZER;	/* new job */
static TAILQ_HEAD(, job) aio_queue = TAILQ_HEAD_INITIALIZER(aio_queue);
static TAILQ_HEAD(, job) aio_running = TAILQ_HEAD_INITIALIZER(aio_running);
static LIST_HEAD(, waiter) aio_waiters = LIST_HEAD_INITIALIZER(aio_waiters);
static int aio_workers;		/* threads in the pool */
static int aio_free_workers;	/* of which waiting for a job */
static int aio_outstanding;	/* requests submitted and not returned
				 * (atomic: aio_return() takes no lock) */
static pthread_once_t aio_once = PTHREAD_ONCE_INIT;

/* aio_lock for the program's threads: see the top of the file. */
static void
lock(sigset_t *old)
{
	sigset_t all;

	sigfillset(&all);
	pthread_sigmask(SIG_BLOCK, &all, old);
	pthread_mutex_lock(&aio_lock);
}

static void
unlock(const sigset_t *old)
{
	pthread_mutex_unlock(&aio_lock);
	pthread_sigmask(SIG_SETMASK, old, NULL);
}

static int
get_state(const struct aiocb *cb)
{
	return __atomic_load_n(&cb->_state, __ATOMIC_ACQUIRE);
}

static void
set_state(struct aiocb *cb, int state)
{
	__atomic_store_n(&cb->_state, state, __ATOMIC_RELEASE);
}

/* A done request whose aiocb is used again is freed, as aio_return() would:
 * 1 if it was one. */
static int
take_done(struct aiocb *cb)
{
	int done = AIO_DONE;

	if (!__atomic_compare_exchange_n(&cb->_state, &done, AIO_NONE, 0,
	    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return 0;
	__atomic_fetch_sub(&aio_outstanding, 1, __ATOMIC_RELAXED);
	return 1;
}

/* The child of fork() has no requests and no workers. */
static void
aio_atfork_prepare(void)
{
	pthread_mutex_lock(&aio_lock);
}

static void
aio_atfork_parent(void)
{
	pthread_mutex_unlock(&aio_lock);
}

static void
aio_atfork_child(void)
{
	struct job *j;

	while ((j = TAILQ_FIRST(&aio_queue)) != NULL) {
		TAILQ_REMOVE(&aio_queue, j, j_link);
		free(j);
	}
	while ((j = TAILQ_FIRST(&aio_running)) != NULL) {
		TAILQ_REMOVE(&aio_running, j, j_link);
		free(j);
	}
	LIST_INIT(&aio_waiters);
	aio_workers = aio_free_workers = 0;
	__atomic_store_n(&aio_outstanding, 0, __ATOMIC_RELAXED);
	pthread_mutex_init(&aio_lock, NULL);
	pthread_cond_init(&aio_work, NULL);
}

static void
aio_init(void)
{
	pthread_atfork(aio_atfork_prepare, aio_atfork_parent,
	    aio_atfork_child);
}

static int
sigevent_valid(const struct sigevent *sev)
{
	switch (sev->sigev_notify) {
	case SIGEV_NONE:
		return 1;
	case SIGEV_SIGNAL:
		return sev->sigev_signo > 0 && sev->sigev_signo < _NSIG;
	case SIGEV_THREAD:
		return sev->sigev_notify_function != NULL;
	default:
		return 0;
	}
}

struct notify_arg {
	void (*func)(union sigval);
	union sigval value;
};

static void *
notify_thread(void *arg)
{
	struct notify_arg na = *(struct notify_arg *)arg;

	free(arg);
	/* It keeps a worker's mask, every signal blocked: a signal to the
	 * process must go to one of the program's threads, not to this one
	 * on its way out (it is no longer the notification). */
	na.func(na.value);
	return NULL;
}

/* Called without aio_lock: a SIGEV_THREAD function may call aio_*(). */
static void
notify(const struct sigevent *sev)
{
	struct notify_arg *na;
	pthread_attr_t attr, *attrp;
	pthread_t t;
	message m;

	switch (sev->sigev_notify) {
	case SIGEV_SIGNAL:
		memset(&m, 0, sizeof(m));
		m.m_lc_pm_sigqueue.pid = getpid();
		m.m_lc_pm_sigqueue.sig = sev->sigev_signo;
		m.m_lc_pm_sigqueue.value = (vir_bytes)sev->sigev_value.sival_ptr;
		m.m_lc_pm_sigqueue.code = SI_ASYNCIO;
		(void)_syscall(PM_PROC_NR, PM_SIGQUEUE, &m);
		break;
	case SIGEV_THREAD:
		if ((na = malloc(sizeof(*na))) == NULL)
			break;
		na->func = sev->sigev_notify_function;
		na->value = sev->sigev_value;
		if (sev->sigev_notify_attributes != NULL)
			attr = *(pthread_attr_t *)sev->sigev_notify_attributes;
		else
			pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		attrp = &attr;
		if (pthread_create(&t, attrp, notify_thread, na) != 0)
			free(na);
		if (sev->sigev_notify_attributes == NULL)
			pthread_attr_destroy(&attr);
		break;
	}
}

/* Wake every thread in aio_suspend() or lio_listio(LIO_WAIT).  The pipes do
 * not block: a full one already has a wakeup pending. */
static void
wake_waiters(void)
{
	struct waiter *w;
	char c = 0;

	LIST_FOREACH(w, &aio_waiters, w_link)
		(void)write(w->w_fd, &c, 1);
}

/*
 * Record the result of a request, done or cancelled, with aio_lock held.
 * Returns the lio_listio() notification to send, if this was the last of
 * a LIO_NOWAIT list; the caller sends that and the request's own.
 */
static struct lio *
complete(struct job *j, int error, ssize_t retval)
{
	struct aiocb *cb = j->j_cb;
	struct lio *l = j->j_lio;

	cb->_retval = retval;
	cb->_errno = error;
	set_state(cb, AIO_DONE);
	wake_waiters();
	if (l != NULL && --l->l_pending == 0 && !l->l_wait)
		return l;
	return NULL;
}

static void
finish(struct job *j, struct lio *l)
{
	struct sigevent sev = j->j_cb->aio_sigevent;

	/* Each request's own, also in a lio_listio(), then the list's. */
	notify(&sev);
	if (l != NULL) {
		notify(&l->l_sig);
		free(l);
	}
	free(j);
}

/* The first queued request whose descriptor has none running, or NULL. */
static struct job *
next_job(void)
{
	struct job *j, *r;

	TAILQ_FOREACH(j, &aio_queue, j_link) {
		TAILQ_FOREACH(r, &aio_running, j_link)
			if (r->j_cb->aio_fildes == j->j_cb->aio_fildes)
				break;
		if (r == NULL)
			return j;
	}
	return NULL;
}

static ssize_t
do_io(struct job *j, int *errp)
{
	struct aiocb *cb = j->j_cb;
	void *buf = (void *)(uintptr_t)cb->aio_buf;
	ssize_t r;
	int flags;

	switch (j->j_op) {
	case OP_READ:
		r = pread(cb->aio_fildes, buf, cb->aio_nbytes, cb->aio_offset);
		if (r < 0 && errno == ESPIPE)
			r = read(cb->aio_fildes, buf, cb->aio_nbytes);
		break;
	case OP_WRITE:
		/* With O_APPEND the data goes at the end, as write() puts it. */
		flags = fcntl(cb->aio_fildes, F_GETFL);
		if (flags != -1 && (flags & O_APPEND))
			r = write(cb->aio_fildes, buf, cb->aio_nbytes);
		else {
			r = pwrite(cb->aio_fildes, buf, cb->aio_nbytes,
			    cb->aio_offset);
			if (r < 0 && errno == ESPIPE)
				r = write(cb->aio_fildes, buf, cb->aio_nbytes);
		}
		break;
	case OP_FDATASYNC:
		r = fdatasync(cb->aio_fildes);
		break;
	default:
		r = fsync(cb->aio_fildes);
		break;
	}
	*errp = r < 0 ? errno : 0;
	return r;
}

static void *
worker(void *arg)
{
	struct job *j;
	struct lio *l;
	ssize_t r;
	int error;

	(void)arg;
	pthread_mutex_lock(&aio_lock);
	for (;;) {
		while ((j = next_job()) == NULL) {
			aio_free_workers++;
			pthread_cond_wait(&aio_work, &aio_lock);
			aio_free_workers--;
		}
		TAILQ_REMOVE(&aio_queue, j, j_link);
		TAILQ_INSERT_TAIL(&aio_running, j, j_link);
		set_state(j->j_cb, AIO_RUNNING);
		pthread_mutex_unlock(&aio_lock);
		r = do_io(j, &error);
		pthread_mutex_lock(&aio_lock);

		TAILQ_REMOVE(&aio_running, j, j_link);
		l = complete(j, error, r);
		/* The next request on that descriptor may go now. */
		pthread_cond_signal(&aio_work);
		pthread_mutex_unlock(&aio_lock);
		finish(j, l);
		pthread_mutex_lock(&aio_lock);
	}
	/* NOTREACHED */
}

/*
 * Start a worker if a request could run but no worker is free, and the pool
 * has room; aio_lock held, so with every signal blocked, which the new thread
 * inherits: the workers, and the threads of SIGEV_THREAD they start, block
 * every signal.
 */
static void
add_worker(void)
{
	pthread_attr_t attr;
	pthread_t t;

	if (aio_free_workers > 0 || aio_workers >= AIO_WORKERS ||
	    next_job() == NULL)
		return;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&t, &attr, worker, NULL) == 0)
		aio_workers++;
	pthread_attr_destroy(&attr);
}

/*
 * Check a request; 0 or the errno its submission fails with.  As POSIX has
 * it, aio_fsync() fails with EBADF, but a bad descriptor for aio_read() or
 * aio_write() is the request's error status, which the I/O itself finds.
 */
static int
check(const struct aiocb *cb, int op)
{
	int flags, mode;

	if (op == OP_FSYNC || op == OP_FDATASYNC) {
		if ((flags = fcntl(cb->aio_fildes, F_GETFL)) == -1)
			return EBADF;
		mode = flags & O_ACCMODE;
		if (mode != O_WRONLY && mode != O_RDWR)
			return EBADF;
	} else {
		if (cb->aio_offset < 0 || cb->aio_nbytes > SSIZE_MAX)
			return EINVAL;
		/* No prioritized I/O: AIO_PRIO_DELTA_MAX is 0. */
		if (cb->aio_reqprio != 0)
			return EINVAL;
	}
	return 0;
}

/* Queue a checked request; aio_lock held.  0 or EAGAIN. */
static int
enqueue(struct aiocb *cb, int op, struct lio *l)
{
	struct job *j;

	/* A request done but never collected with aio_return() is freed
	 * when its aiocb is used again. */
	(void)take_done(cb);
	if (__atomic_load_n(&aio_outstanding, __ATOMIC_RELAXED) >= AIO_MAX)
		return EAGAIN;
	if ((j = malloc(sizeof(*j))) == NULL)
		return EAGAIN;
	j->j_cb = cb;
	j->j_op = op;
	j->j_lio = l;
	cb->_errno = EINPROGRESS;
	cb->_retval = -1;
	set_state(cb, AIO_QUEUED);
	TAILQ_INSERT_TAIL(&aio_queue, j, j_link);
	add_worker();
	if (aio_workers == 0) {
		/* Not one thread to do it: give the request back. */
		TAILQ_REMOVE(&aio_queue, j, j_link);
		free(j);
		set_state(cb, AIO_NONE);
		return EAGAIN;
	}
	__atomic_fetch_add(&aio_outstanding, 1, __ATOMIC_RELAXED);
	pthread_cond_signal(&aio_work);
	return 0;
}

static int
submit(struct aiocb *cb, int op)
{
	sigset_t old;
	int error;

	pthread_once(&aio_once, aio_init);
	if (cb == NULL || !sigevent_valid(&cb->aio_sigevent)) {
		errno = EINVAL;
		return -1;
	}
	if ((error = check(cb, op)) != 0) {
		errno = error;
		return -1;
	}
	lock(&old);
	error = enqueue(cb, op, NULL);
	unlock(&old);
	if (error != 0) {
		errno = error;
		return -1;
	}
	return 0;
}

int
aio_read(struct aiocb *cb)
{
	return submit(cb, OP_READ);
}

int
aio_write(struct aiocb *cb)
{
	return submit(cb, OP_WRITE);
}

int
aio_fsync(int op, struct aiocb *cb)
{
	if (op != O_SYNC && op != O_DSYNC) {
		errno = EINVAL;
		return -1;
	}
	return submit(cb, op == O_SYNC ? OP_FSYNC : OP_FDATASYNC);
}

/* Async-signal-safe: no lock. */
int
aio_error(const struct aiocb *cb)
{
	switch (get_state(cb)) {
	case AIO_QUEUED:
	case AIO_RUNNING:
		return EINPROGRESS;
	case AIO_DONE:
	case AIO_RETURNED:
		return cb->_errno;
	default:
		errno = EINVAL;
		return -1;
	}
}

/* Async-signal-safe: no lock; moving _state from AIO_DONE frees the request
 * once, whoever else tries. */
ssize_t
aio_return(struct aiocb *cb)
{
	ssize_t r;
	int state, error;

	for (;;) {
		state = get_state(cb);
		if (state == AIO_QUEUED || state == AIO_RUNNING) {
			errno = EINPROGRESS;
			return -1;
		}
		if (state != AIO_DONE) {
			errno = EINVAL;
			return -1;
		}
		r = cb->_retval;
		error = cb->_errno;
		if (__atomic_compare_exchange_n(&cb->_state, &state,
		    AIO_RETURNED, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			break;
	}
	__atomic_fetch_sub(&aio_outstanding, 1, __ATOMIC_RELAXED);
	if (r < 0)
		errno = error;
	return r;
}

int
aio_cancel(int fd, struct aiocb *cb)
{
	TAILQ_HEAD(, job) cancelled = TAILQ_HEAD_INITIALIZER(cancelled);
	struct job *j, *next;
	struct lio *l;
	sigset_t old;
	int found = 0, running = 0;

	if (fcntl(fd, F_GETFL) == -1) {
		errno = EBADF;
		return -1;
	}
	if (cb != NULL && cb->aio_fildes != fd) {
		errno = EINVAL;
		return -1;
	}

	lock(&old);
	for (j = TAILQ_FIRST(&aio_queue); j != NULL; j = next) {
		next = TAILQ_NEXT(j, j_link);
		if (j->j_cb->aio_fildes != fd || (cb != NULL && j->j_cb != cb))
			continue;
		TAILQ_REMOVE(&aio_queue, j, j_link);
		TAILQ_INSERT_TAIL(&cancelled, j, j_link);
		found = 1;
	}
	TAILQ_FOREACH(j, &aio_running, j_link)
		if (j->j_cb->aio_fildes == fd && (cb == NULL || j->j_cb == cb))
			running = 1;

	/* Results first, then the notifications, without the lock. */
	while ((j = TAILQ_FIRST(&cancelled)) != NULL) {
		TAILQ_REMOVE(&cancelled, j, j_link);
		l = complete(j, ECANCELED, -1);
		unlock(&old);
		finish(j, l);
		lock(&old);
	}
	unlock(&old);

	if (running)
		return AIO_NOTCANCELED;
	if (found)
		return AIO_CANCELED;
	return AIO_ALLDONE;
}

static int
any_done(const struct aiocb * const list[], int nent)
{
	int i;

	int state;

	for (i = 0; i < nent; i++) {
		if (list[i] == NULL)
			continue;
		state = get_state(list[i]);
		if (state == AIO_DONE || state == AIO_RETURNED)
			return 1;
	}
	return 0;
}

static int
all_done(struct lio *l)
{
	return l->l_pending == 0;
}

/*
 * Wait, with aio_lock held as lock(old) took it, until done() says so, the
 * CLOCK_MONOTONIC deadline passes (EAGAIN) or a signal handler runs (EINTR):
 * the signals are as the caller had them while it waits.
 */
static int
wait_for(int (*done)(const void *, int), const void *arg, int nent,
	const struct timespec *deadline, sigset_t *old)
{
	struct waiter w;
	struct pollfd pfd;
	struct timespec now;
	int p[2], ms, r, error = 0, last = 0;
	char buf[32];
	long long left;

	if (done(arg, nent))
		return 0;
	/* Under a millisecond: that was the look.  The pipe and poll() would
	 * wait behind VFS, busy with the very I/O waited for. */
	if (deadline != NULL) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		if ((deadline->tv_sec - now.tv_sec) * 1000000000LL +
		    (deadline->tv_nsec - now.tv_nsec) < 1000000)
			return EAGAIN;
	}
	if (pipe2(p, O_NONBLOCK | O_CLOEXEC) == -1)
		return EAGAIN;
	w.w_fd = p[1];
	LIST_INSERT_HEAD(&aio_waiters, &w, w_link);
	pfd.fd = p[0];
	pfd.events = POLLIN;

	while (!done(arg, nent)) {
		ms = -1;
		if (deadline != NULL) {
			clock_gettime(CLOCK_MONOTONIC, &now);
			left = (long long)(deadline->tv_sec - now.tv_sec) *
			    1000000000LL + (deadline->tv_nsec - now.tv_nsec);
			if (left <= 0) {
				error = EAGAIN;
				break;
			}
			/* Whole milliseconds, down.  CLOCK_MONOTONIC moves
			 * by clock ticks: under a millisecond left, look
			 * once more and end rather than wait for the tick. */
			left /= 1000000;
			if (left == 0 && last) {
				error = EAGAIN;
				break;
			}
			last = (left == 0);
			ms = left > INT_MAX ? INT_MAX : (int)left;
		}
		unlock(old);
		r = poll(&pfd, 1, ms);
		if (r > 0)
			while (read(p[0], buf, sizeof(buf)) > 0)
				continue;
		lock(old);
		if (r < 0 && errno == EINTR) {
			error = EINTR;
			break;
		}
	}
	LIST_REMOVE(&w, w_link);
	close(p[0]);
	close(p[1]);
	/* What finished meanwhile counts even if the time ran out. */
	if (error != 0 && done(arg, nent))
		error = 0;
	return error;
}

static int
suspend_done(const void *arg, int nent)
{
	return any_done(arg, nent);
}

static int
lio_done(const void *arg, int nent)
{
	(void)nent;
	return all_done((struct lio *)(uintptr_t)arg);
}

/* <aio.h> renames it __aio_suspend50, NetBSD's symbol since 5.0. */
int
aio_suspend(const struct aiocb * const list[], int nent,
	const struct timespec *timeout)
{
	struct timespec deadline;
	sigset_t old;
	int error;

	if (nent < 0) {
		errno = EINVAL;
		return -1;
	}
	if (timeout != NULL) {
		if (timeout->tv_nsec < 0 || timeout->tv_nsec >= 1000000000L ||
		    timeout->tv_sec < 0) {
			errno = EINVAL;
			return -1;
		}
		clock_gettime(CLOCK_MONOTONIC, &deadline);
		deadline.tv_sec += timeout->tv_sec;
		deadline.tv_nsec += timeout->tv_nsec;
		if (deadline.tv_nsec >= 1000000000L) {
			deadline.tv_sec++;
			deadline.tv_nsec -= 1000000000L;
		}
	}
	lock(&old);
	error = wait_for(suspend_done, list, nent,
	    timeout != NULL ? &deadline : NULL, &old);
	unlock(&old);
	if (error != 0) {
		errno = error;
		return -1;
	}
	return 0;
}

int
lio_listio(int mode, struct aiocb * const list[], int nent,
	struct sigevent *sig)
{
	struct lio *l;
	struct aiocb *cb;
	sigset_t old;
	int i, op, error, failed = 0, again = 0;

	pthread_once(&aio_once, aio_init);
	if ((mode != LIO_WAIT && mode != LIO_NOWAIT) || nent < 0 ||
	    nent > AIO_LISTIO_MAX ||
	    (mode == LIO_NOWAIT && sig != NULL && !sigevent_valid(sig))) {
		errno = EINVAL;
		return -1;
	}
	if ((l = malloc(sizeof(*l))) == NULL) {
		errno = EAGAIN;
		return -1;
	}
	l->l_wait = (mode == LIO_WAIT);
	l->l_pending = 1;	/* held until all are queued */
	if (mode == LIO_NOWAIT && sig != NULL)
		l->l_sig = *sig;
	else
		l->l_sig.sigev_notify = SIGEV_NONE;

	lock(&old);
	for (i = 0; i < nent; i++) {
		if ((cb = list[i]) == NULL)
			continue;
		switch (cb->aio_lio_opcode) {
		case LIO_READ:
			op = OP_READ;
			break;
		case LIO_WRITE:
			op = OP_WRITE;
			break;
		case LIO_NOP:
			continue;
		default:
			op = 0;
			break;
		}
		/* A request that cannot be queued fails alone, with its error
		 * status set as aio_read() or aio_write() would have it. */
		if (op == 0)
			error = EINVAL;
		else if ((error = check(cb, op)) == 0)
			error = enqueue(cb, op, l);
		if (error == 0) {
			l->l_pending++;
			continue;
		}
		(void)take_done(cb);
		cb->_errno = error;
		cb->_retval = -1;
		set_state(cb, AIO_DONE);
		__atomic_fetch_add(&aio_outstanding, 1, __ATOMIC_RELAXED);
		failed = 1;
		if (error == EAGAIN)
			again = 1;
	}

	if (mode == LIO_WAIT) {
		l->l_pending--;
		error = wait_for(lio_done, l, 0, NULL, &old);
		if (error == 0) {
			for (i = 0; i < nent && !failed; i++)
				if ((cb = list[i]) != NULL &&
				    cb->aio_lio_opcode != LIO_NOP &&
				    cb->_errno != 0)
					failed = 1;
			free(l);
		} else {
			/* Interrupted: the workers finish the list and the
			 * last one frees it. */
			l->l_wait = 0;
			l->l_sig.sigev_notify = SIGEV_NONE;
			if (l->l_pending == 0)
				free(l);
		}
		unlock(&old);
		if (error != 0) {
			errno = error;
			return -1;
		}
	} else {
		/* Drop the hold: if every request is done already, or there
		 * were none, notify now. */
		if (--l->l_pending > 0)
			l = NULL;
		unlock(&old);
		if (l != NULL) {
			notify(&l->l_sig);
			free(l);
		}
	}
	if (failed) {
		errno = (again && mode == LIO_NOWAIT) ? EAGAIN : EIO;
		return -1;
	}
	return 0;
}
