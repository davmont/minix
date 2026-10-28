/*
 * Process-shared mutexes, condition variables, rwlocks and barriers
 * (PTHREAD_PROCESS_SHARED).
 *
 * The private objects queue their waiters as lists of pthread_t in the
 * object and park them in PM, which works within one process only.  A
 * shared one keeps nothing but words and counters in the object
 * (struct pthread__pmutex and co., pthread_int.h), in the memory the
 * processes share, and its waiters block in the IPC server (psync.c there),
 * which keeps one wait queue or more ("channels") under the id the object
 * got at its initialization.  A wait there names a word in the caller's
 * memory and the value the caller saw in it, and blocks only while it still
 * holds that: whoever changes the word and then wakes cannot be missed, the
 * way a futex works.
 *
 * - A mutex is a word, 0 free, 1 locked, 2 locked with possible waiters, as
 *   in Drepper's "Futexes Are Tricky"; its owner is kept by lwpid, the
 *   thread's endpoint, which no other thread in the system has.
 * - A condition variable is a sequence number that each signal and
 *   broadcast bumps: a waiter blocks while it is the one it saw before it
 *   let the mutex go.
 * - An rwlock and a barrier keep their counters under a lock word of their
 *   own, and wait on sequence numbers too.
 *
 * Such an object lives in the IPC server until its pthread_*_destroy(), as
 * a shared semaphore does until sem_destroy().
 */

#include <sys/cdefs.h>
#include <sys/sched.h>
#include <lib.h>
#include <minix/rs.h>

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <time.h>

#include "pthread.h"
#include "pthread_int.h"

__CTASSERT(sizeof(struct pthread__pmutex) <= sizeof(pthread_mutex_t));
__CTASSERT(sizeof(struct pthread__pcond) <= sizeof(pthread_cond_t));
__CTASSERT(sizeof(struct pthread__prwlock) <= sizeof(pthread_rwlock_t));
__CTASSERT(sizeof(struct pthread__pbarrier) <= sizeof(pthread_barrier_t));

#define	PM(m)	((struct pthread__pmutex *)(void *)(m))
#define	PC(c)	((struct pthread__pcond *)(void *)(c))
#define	PR(r)	((struct pthread__prwlock *)(void *)(r))
#define	PB(b)	((struct pthread__pbarrier *)(void *)(b))

/* The wait queues of an object. */
#define	CHAN_LOCK	0	/* the mutex, or the lock word of the others */
#define	CHAN_READ	1	/* rwlock readers; barrier and condvar waiters */
#define	CHAN_WRITE	2	/* rwlock writers */

#define	WAKE_ALL	INT_MAX

int	_pthread_mutex_held_np(pthread_mutex_t *);

static endpoint_t psync_ep = NONE;

static int
psync_call(int call, message *m)
{
	endpoint_t ep = psync_ep;

	if (ep == NONE) {
		if (minix_rs_lookup("ipc", &ep) != 0) {
			errno = ENOSYS;
			return -1;
		}
		psync_ep = ep;
	}
	return _syscall(ep, call, m);
}

/* A new object's id, or -1 and errno (EAGAIN when the server is full). */
static int
psync_new(void)
{
	message m;

	memset(&m, 0, sizeof(m));
	return psync_call(IPC_PSYNC_INIT, &m);
}

static void
psync_free(int id)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lc_ipc_psync.id = id;
	(void)psync_call(IPC_PSYNC_DESTROY, &m);
}

/*
 * Block while '*word' is 'expected', until a wake on 'chan', the absolute
 * timeout 'ts' on 'clock' or a signal: 0, or an error number (EAGAIN if the
 * word had changed, ETIMEDOUT, EINTR, EINVAL for a bad timeout).
 */
static int
psync_wait(int id, int chan, volatile unsigned int *word,
    unsigned int expected, const struct timespec *ts, clockid_t clock)
{
	message m;
	int serrno = errno, error;

	memset(&m, 0, sizeof(m));
	m.m_lc_ipc_psync.id = id;
	m.m_lc_ipc_psync.chan = chan;
	m.m_lc_ipc_psync.addr = (uint64_t)(uintptr_t)word;
	m.m_lc_ipc_psync.expected = expected;
	if (ts != NULL) {
		m.m_lc_ipc_psync.flags = IPC_PSYNC_TIMED;
		if (clock == CLOCK_MONOTONIC)
			m.m_lc_ipc_psync.flags |= IPC_PSYNC_MONO;
		m.m_lc_ipc_psync.tv_sec = ts->tv_sec;
		m.m_lc_ipc_psync.tv_nsec = (int32_t)ts->tv_nsec;
		if (ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000L)
			m.m_lc_ipc_psync.tv_nsec = -1;	/* EINVAL if it blocks */
	}
	error = (psync_call(IPC_PSYNC_WAIT, &m) < 0) ? errno : 0;
	errno = serrno;
	return error;
}

static void
psync_wake(int id, int chan, int count)
{
	message m;
	int serrno = errno;

	memset(&m, 0, sizeof(m));
	m.m_lc_ipc_psync.id = id;
	m.m_lc_ipc_psync.chan = chan;
	m.m_lc_ipc_psync.count = count;
	(void)psync_call(IPC_PSYNC_WAKE, &m);
	errno = serrno;
}

static unsigned int
cas(volatile unsigned int *p, unsigned int old, unsigned int new)
{

	(void)__atomic_compare_exchange_n(p, &old, new, 0, __ATOMIC_SEQ_CST,
	    __ATOMIC_SEQ_CST);
	return old;
}

/*
 * The lock word of an object: take it, with a timeout or none; 0, ETIMEDOUT
 * or EINVAL.
 */
static int
word_lock(int id, volatile unsigned int *word, const struct timespec *ts)
{
	unsigned int c;
	int error;

	if ((c = cas(word, 0, 1)) == 0)
		return 0;
	if (c != 2)
		c = __atomic_exchange_n(word, 2, __ATOMIC_SEQ_CST);
	while (c != 0) {
		error = psync_wait(id, CHAN_LOCK, word, 2, ts, CLOCK_REALTIME);
		if (error == ETIMEDOUT || error == EINVAL)
			return error;
		c = __atomic_exchange_n(word, 2, __ATOMIC_SEQ_CST);
	}
	return 0;
}

static void
word_unlock(int id, volatile unsigned int *word)
{

	if (__atomic_fetch_sub(word, 1, __ATOMIC_SEQ_CST) != 1) {
		__atomic_store_n(word, 0, __ATOMIC_SEQ_CST);
		psync_wake(id, CHAN_LOCK, 1);
	}
}

static lwpid_t
self_lid(void)
{

	return pthread__self()->pt_lid;
}

/* Mutexes. */

int
pthread__pmutex_init(pthread_mutex_t *ptm, int type, int proto, int ceil)
{
	struct pthread__pmutex *pm = PM(ptm);
	int id;

	if ((id = psync_new()) < 0)
		return errno == ENOSYS ? ENOSYS : EAGAIN;
	memset(ptm, 0, sizeof(*ptm));
	pm->pm_id = id;
	pm->pm_type = (unsigned char)type;
	pm->pm_proto = (unsigned char)proto;
	pm->pm_ceiling = (unsigned char)ceil;
	__atomic_store_n(&pm->pm_magic, _PT_MUTEX_PSHARED_MAGIC,
	    __ATOMIC_SEQ_CST);
	return 0;
}

int
pthread__pmutex_destroy(pthread_mutex_t *ptm)
{
	struct pthread__pmutex *pm = PM(ptm);

	if (pm->pm_word != 0)
		return EBUSY;
	pm->pm_magic = _PT_MUTEX_DEAD;
	psync_free(pm->pm_id);
	return 0;
}

int
pthread__pmutex_held(pthread_mutex_t *ptm)
{
	struct pthread__pmutex *pm = PM(ptm);

	return pm->pm_word != 0 && pm->pm_owner == self_lid();
}

/*
 * Lock, with an absolute CLOCK_REALTIME timeout or none.  A
 * PTHREAD_PRIO_PROTECT mutex runs its owner at its ceiling, as a private one
 * does; PTHREAD_PRIO_INHERIT is not carried from one process to another, and
 * such a mutex works as PTHREAD_PRIO_NONE.
 */
int
pthread__pmutex_lock(pthread_mutex_t *ptm, const struct timespec *ts)
{
	struct pthread__pmutex *pm = PM(ptm);
	lwpid_t self = self_lid();
	int error, serrno;

	if (pm->pm_word != 0 && pm->pm_owner == self) {
		if (pm->pm_type == PTHREAD_MUTEX_RECURSIVE) {
			if (pm->pm_recursed == INT_MAX)
				return EAGAIN;
			pm->pm_recursed++;
			return 0;
		}
		if (pm->pm_type == PTHREAD_MUTEX_ERRORCHECK)
			return EDEADLK;
		/* PTHREAD_MUTEX_NORMAL: deadlock, as POSIX has it. */
	}
	if (pm->pm_proto == PTHREAD_PRIO_PROTECT) {
		serrno = errno;
		if (_sched_protect(pm->pm_ceiling) == -1) {
			error = errno;
			errno = serrno;
			return error;
		}
	}
	if ((error = word_lock(pm->pm_id, &pm->pm_word, ts)) != 0) {
		if (pm->pm_proto == PTHREAD_PRIO_PROTECT)
			(void)_sched_protect(-1);
		return error;
	}
	pm->pm_owner = self;
	return 0;
}

int
pthread__pmutex_trylock(pthread_mutex_t *ptm)
{
	struct pthread__pmutex *pm = PM(ptm);
	lwpid_t self = self_lid();
	int error, serrno;

	if (pm->pm_word != 0 && pm->pm_owner == self &&
	    pm->pm_type == PTHREAD_MUTEX_RECURSIVE) {
		if (pm->pm_recursed == INT_MAX)
			return EAGAIN;
		pm->pm_recursed++;
		return 0;
	}
	if (pm->pm_proto == PTHREAD_PRIO_PROTECT) {
		serrno = errno;
		if (_sched_protect(pm->pm_ceiling) == -1) {
			error = errno;
			errno = serrno;
			return error;
		}
	}
	if (cas(&pm->pm_word, 0, 1) != 0) {
		if (pm->pm_proto == PTHREAD_PRIO_PROTECT)
			(void)_sched_protect(-1);
		return EBUSY;
	}
	pm->pm_owner = self;
	return 0;
}

int
pthread__pmutex_unlock(pthread_mutex_t *ptm)
{
	struct pthread__pmutex *pm = PM(ptm);

	if (pm->pm_word == 0 || pm->pm_owner != self_lid())
		return EPERM;
	if (pm->pm_recursed > 0) {
		pm->pm_recursed--;
		return 0;
	}
	pm->pm_owner = 0;
	word_unlock(pm->pm_id, &pm->pm_word);
	if (pm->pm_proto == PTHREAD_PRIO_PROTECT)
		(void)_sched_protect(-1);
	return 0;
}

int
pthread__pmutex_getprioceiling(const pthread_mutex_t *ptm, int *ceil)
{
	const struct pthread__pmutex *pm = (const void *)ptm;

	if (pm->pm_proto != PTHREAD_PRIO_PROTECT)
		return EINVAL;
	*ceil = pm->pm_ceiling;
	return 0;
}

int
pthread__pmutex_setprioceiling(pthread_mutex_t *ptm, int ceil, int *old_ceil)
{
	struct pthread__pmutex *pm = PM(ptm);
	int error;

	if (pm->pm_proto != PTHREAD_PRIO_PROTECT || (ceil & ~0xff))
		return EINVAL;
	if ((error = pthread__pmutex_lock(ptm, NULL)) != 0)
		return error;
	*old_ceil = pm->pm_ceiling;
	pm->pm_ceiling = (unsigned char)ceil;
	return pthread__pmutex_unlock(ptm);
}

/* Condition variables. */

int
pthread__pcond_init(pthread_cond_t *cond, clockid_t clock)
{
	struct pthread__pcond *pc = PC(cond);
	int id;

	if ((id = psync_new()) < 0)
		return errno == ENOSYS ? ENOSYS : EAGAIN;
	memset(cond, 0, sizeof(*cond));
	pc->pc_id = id;
	pc->pc_clock = clock;
	__atomic_store_n(&pc->pc_magic, _PT_COND_PSHARED_MAGIC,
	    __ATOMIC_SEQ_CST);
	return 0;
}

int
pthread__pcond_destroy(pthread_cond_t *cond)
{
	struct pthread__pcond *pc = PC(cond);

	if (pc->pc_waiters != 0)
		return EBUSY;
	pc->pc_magic = _PT_COND_DEAD;
	psync_free(pc->pc_id);
	return 0;
}

/*
 * The mutex may be a shared one or a private one, which works within one
 * process.  A wakeup that comes from a signal to the thread is spurious, as
 * POSIX allows; the caller looks at its predicate again.
 */
int
pthread__pcond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
    const struct timespec *abstime)
{
	struct pthread__pcond *pc = PC(cond);
	pthread_t self = pthread__self();
	unsigned int seq;
	int error;

	if (!(mutex->ptm_magic == _PT_MUTEX_PSHARED_MAGIC ?
	    pthread__pmutex_held(mutex) : _pthread_mutex_held_np(mutex)))
		return EPERM;
	if (__predict_false(self->pt_cancel))
		pthread__cancelled();

	/* Counted before the sequence is read: a signal that bumps it after
	 * this either finds us counted, or comes before our wait looks. */
	__atomic_fetch_add(&pc->pc_waiters, 1, __ATOMIC_SEQ_CST);
	seq = __atomic_load_n(&pc->pc_seq, __ATOMIC_SEQ_CST);
	pthread_mutex_unlock(mutex);
	error = psync_wait(pc->pc_id, CHAN_READ, &pc->pc_seq, seq, abstime,
	    pc->pc_clock);
	__atomic_fetch_sub(&pc->pc_waiters, 1, __ATOMIC_SEQ_CST);
	pthread_mutex_lock(mutex);

	if (__predict_false(self->pt_cancel))
		pthread__cancelled();
	if (error == ETIMEDOUT || error == EINVAL)
		return error;
	return 0;
}

int
pthread__pcond_signal(pthread_cond_t *cond, int all)
{
	struct pthread__pcond *pc = PC(cond);

	__atomic_fetch_add(&pc->pc_seq, 1, __ATOMIC_SEQ_CST);
	if (__atomic_load_n(&pc->pc_waiters, __ATOMIC_SEQ_CST) != 0)
		psync_wake(pc->pc_id, CHAN_READ, all ? WAKE_ALL : 1);
	return 0;
}

/*
 * Rwlocks.  A writer waiting keeps new readers out, so that writers do not
 * starve; a thread that has the lock for reading and asks for it again
 * while a writer waits deadlocks, as POSIX allows.
 */

int
pthread__prwlock_init(pthread_rwlock_t *ptr)
{
	struct pthread__prwlock *pr = PR(ptr);
	int id;

	if ((id = psync_new()) < 0)
		return errno == ENOSYS ? ENOSYS : EAGAIN;
	memset(ptr, 0, sizeof(*ptr));
	pr->pr_id = id;
	__atomic_store_n(&pr->pr_magic, _PT_RWLOCK_PSHARED_MAGIC,
	    __ATOMIC_SEQ_CST);
	return 0;
}

int
pthread__prwlock_destroy(pthread_rwlock_t *ptr)
{
	struct pthread__prwlock *pr = PR(ptr);
	int busy;

	(void)word_lock(pr->pr_id, &pr->pr_lock, NULL);
	busy = (pr->pr_writer != 0 || pr->pr_readers != 0 ||
	    pr->pr_rwaiters != 0 || pr->pr_wwaiters != 0);
	word_unlock(pr->pr_id, &pr->pr_lock);
	if (busy)
		return EBUSY;
	pr->pr_magic = _PT_RWLOCK_DEAD;
	psync_free(pr->pr_id);
	return 0;
}

/* After a waiting writer gave up: readers it kept out may go now. */
static void
prwlock_let_readers(struct pthread__prwlock *pr)
{

	if (pr->pr_writer == 0 && pr->pr_wwaiters == 0 &&
	    pr->pr_rwaiters != 0) {
		__atomic_fetch_add(&pr->pr_rseq, 1, __ATOMIC_SEQ_CST);
		psync_wake(pr->pr_id, CHAN_READ, WAKE_ALL);
	}
}

int
pthread__prwlock_rdlock(pthread_rwlock_t *ptr, const struct timespec *ts,
    int try)
{
	struct pthread__prwlock *pr = PR(ptr);
	lwpid_t self = self_lid();
	unsigned int seq;
	int error = 0;

	(void)word_lock(pr->pr_id, &pr->pr_lock, NULL);
	while (pr->pr_writer != 0 || pr->pr_wwaiters != 0) {
		if (pr->pr_writer == self) {
			error = EDEADLK;
			break;
		}
		if (try) {
			error = EBUSY;
			break;
		}
		pr->pr_rwaiters++;
		seq = pr->pr_rseq;
		word_unlock(pr->pr_id, &pr->pr_lock);
		error = psync_wait(pr->pr_id, CHAN_READ, &pr->pr_rseq, seq,
		    ts, CLOCK_REALTIME);
		(void)word_lock(pr->pr_id, &pr->pr_lock, NULL);
		pr->pr_rwaiters--;
		if (error == ETIMEDOUT || error == EINVAL)
			break;
		error = 0;
	}
	if (error == 0) {
		if (pr->pr_readers == UINT_MAX)
			error = EAGAIN;
		else
			pr->pr_readers++;
	}
	word_unlock(pr->pr_id, &pr->pr_lock);
	return error;
}

int
pthread__prwlock_wrlock(pthread_rwlock_t *ptr, const struct timespec *ts,
    int try)
{
	struct pthread__prwlock *pr = PR(ptr);
	lwpid_t self = self_lid();
	unsigned int seq;
	int error = 0;

	(void)word_lock(pr->pr_id, &pr->pr_lock, NULL);
	while (pr->pr_writer != 0 || pr->pr_readers != 0) {
		if (pr->pr_writer == self) {
			error = EDEADLK;
			break;
		}
		if (try) {
			error = EBUSY;
			break;
		}
		pr->pr_wwaiters++;
		seq = pr->pr_wseq;
		word_unlock(pr->pr_id, &pr->pr_lock);
		error = psync_wait(pr->pr_id, CHAN_WRITE, &pr->pr_wseq, seq,
		    ts, CLOCK_REALTIME);
		(void)word_lock(pr->pr_id, &pr->pr_lock, NULL);
		pr->pr_wwaiters--;
		if (error == ETIMEDOUT || error == EINVAL) {
			prwlock_let_readers(pr);
			break;
		}
		error = 0;
	}
	if (error == 0)
		pr->pr_writer = self;
	word_unlock(pr->pr_id, &pr->pr_lock);
	return error;
}

int
pthread__prwlock_unlock(pthread_rwlock_t *ptr)
{
	struct pthread__prwlock *pr = PR(ptr);
	int error = 0;

	(void)word_lock(pr->pr_id, &pr->pr_lock, NULL);
	if (pr->pr_writer != 0) {
		if (pr->pr_writer != self_lid())
			error = EPERM;
		else
			pr->pr_writer = 0;
	} else if (pr->pr_readers != 0)
		pr->pr_readers--;
	else
		error = EPERM;
	if (error == 0 && pr->pr_writer == 0 && pr->pr_readers == 0) {
		if (pr->pr_wwaiters != 0) {
			__atomic_fetch_add(&pr->pr_wseq, 1, __ATOMIC_SEQ_CST);
			psync_wake(pr->pr_id, CHAN_WRITE, 1);
		} else if (pr->pr_rwaiters != 0) {
			__atomic_fetch_add(&pr->pr_rseq, 1, __ATOMIC_SEQ_CST);
			psync_wake(pr->pr_id, CHAN_READ, WAKE_ALL);
		}
	}
	word_unlock(pr->pr_id, &pr->pr_lock);
	return error;
}

/* Barriers. */

int
pthread__pbarrier_init(pthread_barrier_t *barrier, unsigned int count)
{
	struct pthread__pbarrier *pb = PB(barrier);
	int id;

	if ((id = psync_new()) < 0)
		return errno == ENOSYS ? ENOSYS : EAGAIN;
	memset(barrier, 0, sizeof(*barrier));
	pb->pb_id = id;
	pb->pb_count = count;
	__atomic_store_n(&pb->pb_magic, _PT_BARRIER_PSHARED_MAGIC,
	    __ATOMIC_SEQ_CST);
	return 0;
}

int
pthread__pbarrier_destroy(pthread_barrier_t *barrier)
{
	struct pthread__pbarrier *pb = PB(barrier);
	int busy;

	(void)word_lock(pb->pb_id, &pb->pb_lock, NULL);
	busy = (pb->pb_cur != 0);
	word_unlock(pb->pb_id, &pb->pb_lock);
	if (busy)
		return EBUSY;
	pb->pb_magic = _PT_BARRIER_DEAD;
	psync_free(pb->pb_id);
	return 0;
}

/* A signal does not end the wait (POSIX: the thread goes on waiting). */
int
pthread__pbarrier_wait(pthread_barrier_t *barrier)
{
	struct pthread__pbarrier *pb = PB(barrier);
	unsigned int gen;

	(void)word_lock(pb->pb_id, &pb->pb_lock, NULL);
	gen = pb->pb_gen;
	if (++pb->pb_cur == pb->pb_count) {
		pb->pb_cur = 0;
		__atomic_fetch_add(&pb->pb_gen, 1, __ATOMIC_SEQ_CST);
		psync_wake(pb->pb_id, CHAN_READ, WAKE_ALL);
		word_unlock(pb->pb_id, &pb->pb_lock);
		return PTHREAD_BARRIER_SERIAL_THREAD;
	}
	word_unlock(pb->pb_id, &pb->pb_lock);
	while (__atomic_load_n(&pb->pb_gen, __ATOMIC_SEQ_CST) == gen)
		(void)psync_wait(pb->pb_id, CHAN_READ, &pb->pb_gen, gen, NULL,
		    CLOCK_REALTIME);
	return 0;
}
