/*
 * Wait queues for libpthread's process-shared objects: mutexes, condition
 * variables, rwlocks and barriers with PTHREAD_PROCESS_SHARED.  The object
 * itself, its lock word and counters, lives in memory the processes share;
 * only the threads blocked on it are kept here, as a futex does.
 *
 * An object gets an id here when it is initialized, which libpthread keeps
 * in it, so that it is the same object in every process that maps it,
 * wherever it is mapped, and also after its page has been swapped out and
 * back in.  A wait names the object, one of its queues ('chan') and a 32-bit
 * word in the caller's memory: it blocks only while the word still holds the
 * value the caller saw, so that a wake that came after the caller looked at
 * the word but before its wait got here is not lost.  It ends with a wake on
 * that queue (the longest waiting first), its timeout (ETIMEDOUT), or a
 * signal (PM process events, EINTR, as for semop); libpthread then looks at
 * the word again.
 *
 * As for semaphores (psem.c) nothing counts the users of an object: it lives
 * until its pthread_*_destroy().
 */
#include "inc.h"

#include <limits.h>

#define PSYNC_MAX	4096		/* objects in the system */

struct psync {
	int		used;
	unsigned int	gen;		/* bumped when the slot is reused */
};

/* A thread blocked in a wait, by process slot. */
struct psyncwait {
	struct psync	*ps;		/* NULL: none */
	int		chan;
	endpoint_t	endpt;
	unsigned long	seq;		/* arrival order */
	int		timed;
	minix_timer_t	tmr;
};

static struct psync psync_list[PSYNC_MAX];
static struct psyncwait psync_wait[NR_PROCS];
static unsigned long psync_seq;
static int psync_timers_ready;

static void
psync_init(void)
{
	int i;

	if (psync_timers_ready)
		return;
	for (i = 0; i < NR_PROCS; i++)
		init_timer(&psync_wait[i].tmr);
	psync_timers_ready = 1;
}

/* Do we need PM's process events (blocked callers)? */
static void
psync_update_sub(void)
{
	int i, want = 0;

	for (i = 0; i < NR_PROCS && !want; i++)
		if (psync_wait[i].ps != NULL)
			want = 1;
	update_psync_sub(want);
}

/* The id of an object: positive, and not that of an earlier one in the same
 * slot. */
static int
psync_id(struct psync *ps)
{

	return (int)(((ps->gen & 0x7fff) << 16) | (ps - psync_list + 1));
}

static struct psync *
psync_lookup(int id)
{
	struct psync *ps;
	int ix = (id & 0xffff) - 1;

	if (id <= 0 || ix < 0 || ix >= PSYNC_MAX)
		return NULL;
	ps = &psync_list[ix];
	if (!ps->used || psync_id(ps) != id)
		return NULL;
	return ps;
}

static int
psync_has_waiters(struct psync *ps)
{
	int i;

	for (i = 0; i < NR_PROCS; i++)
		if (psync_wait[i].ps == ps)
			return 1;
	return 0;
}

static void
psync_reply(struct psyncwait *w, int r)
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
psync_timeout(int arg)
{
	struct psyncwait *w;

	if (arg < 0 || arg >= NR_PROCS)
		return;
	w = &psync_wait[arg];
	if (w->ps == NULL)
		return;
	w->timed = 0;				/* the timer is gone already */
	psync_reply(w, ETIMEDOUT);
	psync_update_sub();
}

int
do_psync_init(message *m __unused)
{
	struct psync *ps;
	unsigned int gen;
	int i;

	psync_init();
	for (i = 0; i < PSYNC_MAX && psync_list[i].used; i++)
		;
	if (i == PSYNC_MAX)
		return EAGAIN;			/* pthread_mutex_init() and co. */
	ps = &psync_list[i];
	gen = ps->gen;
	memset(ps, 0, sizeof(*ps));
	ps->gen = gen;
	ps->used = 1;
	return psync_id(ps);
}

int
do_psync_destroy(message *m)
{
	struct psync *ps;

	psync_init();
	if ((ps = psync_lookup(m->m_lc_ipc_psync.id)) == NULL)
		return EINVAL;
	if (psync_has_waiters(ps))
		return EBUSY;
	ps->used = 0;
	ps->gen++;
	return OK;
}

/*
 * Block the caller while the word at 'addr' holds 'expected': EAGAIN at once
 * if it does not, ETIMEDOUT when the timeout has passed, and EINVAL for an
 * invalid timeout (only when the call would block, as POSIX has it for the
 * calls that come here).
 */
int
do_psync_wait(message *m)
{
	struct psync *ps;
	struct psyncwait *w;
	clock_t uptime, realtime, ticks;
	time_t boottime;
	int64_t sec, now_ticks, deadline;
	uint32_t word;
	long nsec;
	int r, hz;

	psync_init();
	if ((ps = psync_lookup(m->m_lc_ipc_psync.id)) == NULL)
		return EINVAL;

	/* Nothing can change the word between this look and the wait: a wake
	 * comes here after the word has changed, so after this call. */
	if (sys_datacopy(m->m_source, (vir_bytes)m->m_lc_ipc_psync.addr, SELF,
	    (vir_bytes)&word, sizeof(word)) != OK)
		return EFAULT;
	if (word != m->m_lc_ipc_psync.expected)
		return EAGAIN;

	ticks = 0;
	if (m->m_lc_ipc_psync.flags & IPC_PSYNC_TIMED) {
		sec = m->m_lc_ipc_psync.tv_sec;
		nsec = m->m_lc_ipc_psync.tv_nsec;
		if (nsec < 0 || nsec >= 1000000000L)
			return EINVAL;
		if ((r = getuptime(&uptime, &realtime, &boottime)) != OK)
			return r;
		hz = sys_hz();
		if (m->m_lc_ipc_psync.flags & IPC_PSYNC_MONO)
			now_ticks = uptime;
		else
			now_ticks = (int64_t)boottime * hz + realtime;
		deadline = sec * hz + ((int64_t)nsec * hz + 999999999) /
		    1000000000;
		if (deadline <= now_ticks)
			return ETIMEDOUT;
		if (deadline - now_ticks > TMRDIFF_MAX)
			deadline = now_ticks + TMRDIFF_MAX;
		ticks = (clock_t)(deadline - now_ticks);
	}

	w = &psync_wait[_ENDPOINT_P(m->m_source)];
	w->ps = ps;
	w->chan = m->m_lc_ipc_psync.chan;
	w->endpt = m->m_source;
	w->seq = ++psync_seq;
	w->timed = (ticks > 0);
	if (w->timed)
		set_timer(&w->tmr, ticks, psync_timeout,
		    (int)_ENDPOINT_P(m->m_source));
	psync_update_sub();
	return SUSPEND;
}

/* Wake up to 'count' of the threads on a queue, the longest waiting first;
 * the result is how many. */
int
do_psync_wake(message *m)
{
	struct psync *ps;
	struct psyncwait *w, *best;
	int i, n, count, chan;

	psync_init();
	if ((ps = psync_lookup(m->m_lc_ipc_psync.id)) == NULL)
		return EINVAL;
	count = m->m_lc_ipc_psync.count;
	chan = m->m_lc_ipc_psync.chan;

	for (n = 0; n < count; n++) {
		best = NULL;
		for (i = 0; i < NR_PROCS; i++) {
			w = &psync_wait[i];
			if (w->ps == ps && w->chan == chan &&
			    (best == NULL || w->seq < best->seq))
				best = w;
		}
		if (best == NULL)
			break;
		psync_reply(best, OK);
	}
	if (n > 0)
		psync_update_sub();
	return n;
}

/*
 * PM told us about a process being signaled or exiting.  A blocked wait is
 * cancelled: with EINTR for a signal, without reply for an exit.
 */
void
psync_process_event(endpoint_t endpt, int has_exited)
{
	struct psyncwait *w;
	unsigned int slot;

	slot = _ENDPOINT_P(endpt);
	if (slot >= NR_PROCS)
		return;
	w = &psync_wait[slot];
	if (w->ps != NULL && w->endpt == endpt) {
		if (has_exited) {
			if (w->timed)
				cancel_timer(&w->tmr);
			w->ps = NULL;
		} else
			psync_reply(w, EINTR);
	}
	psync_update_sub();
}
