/*
 * POSIX semaphores that more than one process uses, for the IPC server: named
 * ones (sem_open and co.) and unnamed ones in shared memory (sem_init with a
 * nonzero pshared).  A process-private unnamed semaphore never comes here;
 * libpthread keeps it on a mutex and a condition variable.
 *
 * As for message queues (mqueue.c), libpthread's sem_open() opens a token
 * file under /var/sem for the semaphore's name, its permissions and its
 * identity, and the token's (dev, ino) keys the semaphore held here.  The
 * token's descriptor is closed at once: a semaphore is known by its id, which
 * libpthread keeps in the sem_t, so that it works in any process that has the
 * sem_t, through fork() or shared memory.
 *
 * sem_wait() on a semaphore at zero blocks the caller until a sem_post() for
 * it (the longest waiting first), its timeout passes, or a signal interrupts
 * it (PM process events, as for semop: EINTR).  Semaphores are not reference
 * counted, as VFS does not tell us about exits from shared memory, nor does
 * libpthread about sem_close() in a process that exits.  A named semaphore
 * lives until sem_unlink(), then until its slot is needed while nobody is
 * blocked on it; an unnamed one until sem_destroy().
 */
#include "inc.h"

#include <limits.h>

#define PSEM_MAX	256		/* semaphores in the system */

struct psem {
	int		used;
	int		named;
	int		unlinked;
	uint64_t	dev;		/* named: the token file */
	uint64_t	ino;
	unsigned int	gen;		/* bumped when the slot is reused */
	unsigned int	value;		/* at most INT_MAX: sem_getvalue() */
};

/* A process blocked in sem_wait(), by process slot. */
struct psemwait {
	struct psem	*ps;		/* NULL: none */
	endpoint_t	endpt;
	unsigned long	seq;		/* arrival order */
	int		timed;
	minix_timer_t	tmr;
};

static struct psem psem_list[PSEM_MAX];
static struct psemwait psem_wait[NR_PROCS];
static unsigned long psem_seq;
static int psem_timers_ready;

static void
psem_init(void)
{
	int i;

	if (psem_timers_ready)
		return;
	for (i = 0; i < NR_PROCS; i++)
		init_timer(&psem_wait[i].tmr);
	psem_timers_ready = 1;
}

/* Do we need PM's process events (blocked callers)? */
static void
psem_update_sub(void)
{
	int i, want = 0;

	for (i = 0; i < NR_PROCS && !want; i++)
		if (psem_wait[i].ps != NULL)
			want = 1;
	update_psem_sub(want);
}

/* The id of a semaphore: positive, and not that of an earlier one in the same
 * slot. */
static int
psem_id(struct psem *ps)
{

	return (int)(((ps->gen & 0x7fff) << 16) | (ps - psem_list + 1));
}

static struct psem *
psem_lookup(int id)
{
	struct psem *ps;
	int ix = (id & 0xffff) - 1;

	if (id <= 0 || ix < 0 || ix >= PSEM_MAX)
		return NULL;
	ps = &psem_list[ix];
	if (!ps->used || psem_id(ps) != id)
		return NULL;
	return ps;
}

static int
psem_has_waiters(struct psem *ps)
{
	int i;

	for (i = 0; i < NR_PROCS; i++)
		if (psem_wait[i].ps == ps)
			return 1;
	return 0;
}

static void
psem_free(struct psem *ps)
{
	unsigned int gen = ps->gen + 1;

	assert(!psem_has_waiters(ps));
	memset(ps, 0, sizeof(*ps));
	ps->gen = gen;
}

/* A free slot, making room if need be: unlinked named semaphores that nobody
 * is blocked on go. */
static struct psem *
psem_alloc(unsigned int value)
{
	struct psem *ps;
	unsigned int gen;
	int i;

	for (i = 0; i < PSEM_MAX && psem_list[i].used; i++)
		;
	if (i == PSEM_MAX) {
		for (i = 0; i < PSEM_MAX; i++)
			if (psem_list[i].used && psem_list[i].unlinked &&
			    !psem_has_waiters(&psem_list[i]))
				psem_free(&psem_list[i]);
		for (i = 0; i < PSEM_MAX && psem_list[i].used; i++)
			;
		if (i == PSEM_MAX)
			return NULL;
	}
	ps = &psem_list[i];
	gen = ps->gen;
	memset(ps, 0, sizeof(*ps));
	ps->gen = gen;
	ps->used = 1;
	ps->value = value;
	return ps;
}

static void
psem_reply(struct psemwait *w, int r)
{
	message m;

	if (w->timed)
		cancel_timer(&w->tmr);
	memset(&m, 0, sizeof(m));
	m.m_type = r;
	ipc_sendnb(w->endpt, &m);
	w->ps = NULL;
}

static void
psem_timeout(int arg)
{
	struct psemwait *w;

	if (arg < 0 || arg >= NR_PROCS)
		return;
	w = &psem_wait[arg];
	if (w->ps == NULL)
		return;
	w->timed = 0;				/* the timer is gone already */
	psem_reply(w, ETIMEDOUT);
	psem_update_sub();
}

int
do_psem_open(message *m)
{
	struct psem *ps;
	int i, flags;

	psem_init();
	flags = m->m_lc_ipc_psem.flags;
	/* An unlinked one has no name any more, even if a new token file has
	 * its inode now. */
	for (i = 0, ps = NULL; i < PSEM_MAX; i++) {
		if (psem_list[i].used && psem_list[i].named &&
		    !psem_list[i].unlinked &&
		    psem_list[i].dev == m->m_lc_ipc_psem.dev &&
		    psem_list[i].ino == m->m_lc_ipc_psem.ino) {
			ps = &psem_list[i];
			break;
		}
	}

	/* A just-created token file whose key we know is a reused inode: the
	 * old semaphore's token was removed behind our back.
	 */
	if (ps != NULL && (flags & IPC_PSEM_FRESH)) {
		if (psem_has_waiters(ps))
			return EBUSY;		/* cannot happen with a fresh file */
		psem_free(ps);
		ps = NULL;
	}
	if (ps != NULL)
		return psem_id(ps);
	if (!(flags & IPC_PSEM_CREAT))
		return ENOENT;
	if (m->m_lc_ipc_psem.value > INT_MAX)
		return EINVAL;
	if ((ps = psem_alloc(m->m_lc_ipc_psem.value)) == NULL)
		return ENOSPC;
	ps->named = 1;
	ps->dev = m->m_lc_ipc_psem.dev;
	ps->ino = m->m_lc_ipc_psem.ino;
	return psem_id(ps);
}

int
do_psem_init(message *m)
{
	struct psem *ps;

	psem_init();
	if (m->m_lc_ipc_psem.value > INT_MAX)
		return EINVAL;
	if ((ps = psem_alloc(m->m_lc_ipc_psem.value)) == NULL)
		return ENOSPC;
	return psem_id(ps);
}

int
do_psem_destroy(message *m)
{
	struct psem *ps;

	psem_init();
	if ((ps = psem_lookup(m->m_lc_ipc_psem.id)) == NULL || ps->named)
		return EINVAL;
	if (psem_has_waiters(ps))
		return EBUSY;			/* as on NetBSD */
	psem_free(ps);
	return OK;
}

int
do_psem_unlink(message *m)
{
	int i;

	psem_init();
	for (i = 0; i < PSEM_MAX; i++)
		if (psem_list[i].used && psem_list[i].named &&
		    !psem_list[i].unlinked &&
		    psem_list[i].dev == m->m_lc_ipc_psem.dev &&
		    psem_list[i].ino == m->m_lc_ipc_psem.ino)
			psem_list[i].unlinked = 1;	/* lives on while used */
	return OK;
}

/*
 * Take the semaphore, or block the caller until it can: fail with EAGAIN for
 * sem_trywait(), ETIMEDOUT when the timeout has passed, and EINVAL for an
 * invalid timeout (only when the call would block, as POSIX has it).
 */
int
do_psem_wait(message *m)
{
	struct psem *ps;
	struct psemwait *w;
	clock_t uptime, realtime, ticks;
	time_t boottime;
	int64_t sec, now_ticks, deadline;
	long nsec;
	int r, hz;

	psem_init();
	if ((ps = psem_lookup(m->m_lc_ipc_psem.id)) == NULL)
		return EINVAL;
	if (ps->value > 0) {
		ps->value--;
		return OK;
	}
	if (m->m_lc_ipc_psem.flags & IPC_PSEM_TRY)
		return EAGAIN;

	w = &psem_wait[_ENDPOINT_P(m->m_source)];
	ticks = 0;
	if (m->m_lc_ipc_psem.flags & IPC_PSEM_TIMED) {
		sec = m->m_lc_ipc_psem.tv_sec;
		nsec = m->m_lc_ipc_psem.tv_nsec;
		if (nsec < 0 || nsec >= 1000000000L)
			return EINVAL;
		if ((r = getuptime(&uptime, &realtime, &boottime)) != OK)
			return r;
		hz = sys_hz();
		now_ticks = (int64_t)boottime * hz + realtime;
		deadline = sec * hz + ((int64_t)nsec * hz + 999999999) /
		    1000000000;
		if (deadline <= now_ticks)
			return ETIMEDOUT;
		if (deadline - now_ticks > TMRDIFF_MAX)
			deadline = now_ticks + TMRDIFF_MAX;
		ticks = (clock_t)(deadline - now_ticks);
	}

	w->ps = ps;
	w->endpt = m->m_source;
	w->seq = ++psem_seq;
	w->timed = (ticks > 0);
	if (w->timed)
		set_timer(&w->tmr, ticks, psem_timeout,
		    (int)_ENDPOINT_P(m->m_source));
	psem_update_sub();
	return SUSPEND;
}

int
do_psem_post(message *m)
{
	struct psem *ps;
	struct psemwait *w, *best = NULL;
	int i;

	psem_init();
	if ((ps = psem_lookup(m->m_lc_ipc_psem.id)) == NULL)
		return EINVAL;

	/* A waiter is there only while the value is zero: it takes the post. */
	for (i = 0; i < NR_PROCS; i++) {
		w = &psem_wait[i];
		if (w->ps == ps && (best == NULL || w->seq < best->seq))
			best = w;
	}
	if (best != NULL) {
		psem_reply(best, OK);
		psem_update_sub();
		return OK;
	}
	if (ps->value >= INT_MAX)
		return EOVERFLOW;
	ps->value++;
	return OK;
}

int
do_psem_getvalue(message *m)
{
	struct psem *ps;

	psem_init();
	if ((ps = psem_lookup(m->m_lc_ipc_psem.id)) == NULL)
		return EINVAL;
	return (int)ps->value;
}

/*
 * PM told us about a process being signaled or exiting.  A blocked sem_wait()
 * is cancelled: with EINTR for a signal, without reply for an exit.
 */
void
psem_process_event(endpoint_t endpt, int has_exited)
{
	struct psemwait *w;
	unsigned int slot;

	slot = _ENDPOINT_P(endpt);
	if (slot >= NR_PROCS)
		return;
	w = &psem_wait[slot];
	if (w->ps != NULL && w->endpt == endpt) {
		if (has_exited) {
			if (w->timed)
				cancel_timer(&w->tmr);
			w->ps = NULL;
		} else
			psem_reply(w, EINTR);
	}
	psem_update_sub();
}
