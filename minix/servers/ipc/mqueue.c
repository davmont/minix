/*
 * POSIX message queues (mq_open and co.) for the IPC server.
 *
 * As for POSIX shm (posix_shm.c), libc's mq_open() opens a token file, under
 * /var/mqueue, for the queue's name, its permissions and its identity: the
 * token's (dev, ino) keys the queue held here, and its file descriptor is the
 * mqd_t.  Descriptors are thereby inherited across fork, closed on exec (libc
 * sets FD_CLOEXEC) and passed over sockets like any other fd; the access mode
 * and O_NONBLOCK of a descriptor are those of its open file, which libc reads
 * with fcntl() and passes along.
 *
 * Messages are kept in priority order, first in first out within a priority.
 * A full (or empty) queue blocks the caller, unless the descriptor is
 * non-blocking, until there is room (a message), its timeout passes, or a
 * signal interrupts it (PM process events, as for semop).  One process may be
 * registered for notification (mq_notify) of a message arriving at an empty
 * queue that nobody is waiting on; PM then sends it the signal it asked for,
 * with SI_MESGQ and its sigev_value.
 *
 * Queues are not reference counted: closing a descriptor happens in VFS, which
 * does not tell us.  A queue therefore lives until mq_unlink() and then until
 * its key is used for a new token file, or its slot is needed for a new queue
 * while nobody is blocked on it.  Memory is bounded by reserving maxmsg times
 * msgsize for every queue when it is created.
 */
#include "inc.h"

#include <minix/timers.h>

#define MQ_MAX			64	/* queues in the system */
#define MQ_MAXMSG_MAX		1024	/* per queue */
#define MQ_MSGSIZE_MAX		65536
#define MQ_QUEUE_BYTES_MAX	(1024 * 1024)	/* maxmsg * msgsize */
#define MQ_TOTAL_BYTES_MAX	(8 * 1024 * 1024)
#define MQ_DEF_MAXMSG		10
#define MQ_DEF_MSGSIZE		8192
#define MQ_PRIO_LIMIT		32	/* MQ_PRIO_MAX */

#define MQ_EVENTS		0x02	/* see main.c */

struct mqmsg {
	struct mqmsg	*next;
	unsigned int	prio;
	size_t		len;
	char		data[];
};

struct mq {
	int		used;
	int		unlinked;
	uint64_t	dev;
	uint64_t	ino;
	long		maxmsg;
	long		msgsize;
	long		curmsgs;
	struct mqmsg	*head;		/* highest priority first */
	/* mq_notify() registration */
	endpoint_t	nt_endpt;	/* NONE: nobody registered */
	pid_t		nt_pid;
	int		nt_signo;	/* 0: SIGEV_NONE */
	vir_bytes	nt_value;
};

/* A process blocked in mq_send() or mq_receive(), by process slot. */
#define OP_NONE		0
#define OP_SEND		1
#define OP_RECEIVE	2
struct mqwait {
	int		op;
	endpoint_t	endpt;
	struct mq	*mq;
	vir_bytes	buf;
	size_t		len;
	unsigned int	prio;
	unsigned long	seq;		/* arrival order */
	int		timed;
	minix_timer_t	tmr;
};

static struct mq mq_list[MQ_MAX];
static struct mqwait mq_wait[NR_PROCS];
static unsigned long mq_seq;
static size_t mq_reserved;		/* sum of maxmsg * msgsize */
static int mq_timers_ready;

static void
mq_init(void)
{
	int i;

	if (mq_timers_ready)
		return;
	for (i = 0; i < NR_PROCS; i++)
		init_timer(&mq_wait[i].tmr);
	for (i = 0; i < MQ_MAX; i++)
		mq_list[i].nt_endpt = NONE;
	mq_timers_ready = 1;
}

/* Do we need PM's process events (blocked callers, registrations)? */
static void
mq_update_sub(void)
{
	int i, want = 0;

	for (i = 0; i < NR_PROCS && !want; i++)
		if (mq_wait[i].op != OP_NONE)
			want = 1;
	for (i = 0; i < MQ_MAX && !want; i++)
		if (mq_list[i].used && mq_list[i].nt_endpt != NONE)
			want = 1;
	update_mq_sub(want);
}

static struct mq *
mq_find(uint64_t dev, uint64_t ino)
{
	int i;

	for (i = 0; i < MQ_MAX; i++)
		if (mq_list[i].used && mq_list[i].dev == dev &&
		    mq_list[i].ino == ino)
			return &mq_list[i];
	return NULL;
}

static int
mq_has_waiters(struct mq *mq)
{
	int i;

	for (i = 0; i < NR_PROCS; i++)
		if (mq_wait[i].op != OP_NONE && mq_wait[i].mq == mq)
			return 1;
	return 0;
}

static void
mq_free(struct mq *mq)
{
	struct mqmsg *msg;

	assert(!mq_has_waiters(mq));
	while ((msg = mq->head) != NULL) {
		mq->head = msg->next;
		free(msg);
	}
	mq_reserved -= (size_t)mq->maxmsg * mq->msgsize;
	memset(mq, 0, sizeof(*mq));
	mq->nt_endpt = NONE;
}

/* Wake a blocked caller with a reply. */
static void
mq_reply(struct mqwait *w, int r, ssize_t len, unsigned int prio)
{
	message m;

	if (w->timed)
		cancel_timer(&w->tmr);
	memset(&m, 0, sizeof(m));
	m.m_type = r;
	m.m_ipc_lc_mq.len = len;
	m.m_ipc_lc_mq.prio = prio;
	ipc_sendnb(w->endpt, &m);
	w->op = OP_NONE;
	w->mq = NULL;
}

/* The longest waiting caller blocked on 'mq' in 'op', or NULL. */
static struct mqwait *
mq_oldest(struct mq *mq, int op)
{
	struct mqwait *w, *best = NULL;
	int i;

	for (i = 0; i < NR_PROCS; i++) {
		w = &mq_wait[i];
		if (w->op == op && w->mq == mq &&
		    (best == NULL || w->seq < best->seq))
			best = w;
	}
	return best;
}

/* Copy a message in from 'endpt' and queue it. */
static int
mq_enqueue(struct mq *mq, endpoint_t endpt, vir_bytes buf, size_t len,
	unsigned int prio)
{
	struct mqmsg *msg, **pp;
	int r, was_empty;

	if ((msg = malloc(sizeof(*msg) + len)) == NULL)
		return ENOMEM;
	if (len > 0 && (r = sys_datacopy(endpt, buf, SELF, (vir_bytes)msg->data,
	    len)) != OK) {
		free(msg);
		return r;
	}
	msg->prio = prio;
	msg->len = len;
	for (pp = &mq->head; *pp != NULL && (*pp)->prio >= prio;
	    pp = &(*pp)->next)
		;
	msg->next = *pp;
	*pp = msg;
	was_empty = (mq->curmsgs++ == 0);

	/* POSIX: notify the registered process of a message arriving at an
	 * empty queue, unless somebody is waiting to receive it.
	 */
	if (was_empty && mq->nt_endpt != NONE &&
	    mq_oldest(mq, OP_RECEIVE) == NULL) {
		if (mq->nt_signo != 0)
			(void)srv_sigqueue(mq->nt_pid, mq->nt_signo, SI_MESGQ,
			    mq->nt_value);
		mq->nt_endpt = NONE;		/* the registration is used up */
	}
	return OK;
}

/* Take the first message off the queue and copy it out to 'endpt'. */
static int
mq_dequeue(struct mq *mq, endpoint_t endpt, vir_bytes buf, ssize_t *lenp,
	unsigned int *priop)
{
	struct mqmsg *msg;
	int r;

	msg = mq->head;
	assert(msg != NULL);
	if (msg->len > 0 && (r = sys_datacopy(SELF, (vir_bytes)msg->data,
	    endpt, buf, msg->len)) != OK)
		return r;			/* the message stays */
	mq->head = msg->next;
	mq->curmsgs--;
	*lenp = msg->len;
	*priop = msg->prio;
	free(msg);
	return OK;
}

/* Something changed on 'mq': let blocked callers go on while they can. */
static void
mq_run(struct mq *mq)
{
	struct mqwait *w;
	ssize_t len;
	unsigned int prio;
	int r, progress;

	do {
		progress = 0;
		if (mq->curmsgs > 0 &&
		    (w = mq_oldest(mq, OP_RECEIVE)) != NULL) {
			r = mq_dequeue(mq, w->endpt, w->buf, &len, &prio);
			mq_reply(w, r, r == OK ? len : 0, r == OK ? prio : 0);
			progress = 1;
		}
		if (mq->curmsgs < mq->maxmsg &&
		    (w = mq_oldest(mq, OP_SEND)) != NULL) {
			r = mq_enqueue(mq, w->endpt, w->buf, w->len, w->prio);
			mq_reply(w, r, 0, 0);
			progress = 1;
		}
	} while (progress);
	mq_update_sub();
}

static void
mq_timeout(int arg)
{
	struct mqwait *w;

	if (arg < 0 || arg >= NR_PROCS)
		return;
	w = &mq_wait[arg];
	if (w->op == OP_NONE)
		return;
	w->timed = 0;				/* the timer is gone already */
	mq_reply(w, ETIMEDOUT, 0, 0);
	mq_update_sub();
}

/*
 * Block the caller of a send or receive that cannot go on now, or fail it:
 * EAGAIN for a non-blocking descriptor, ETIMEDOUT when the timeout has
 * passed, EINVAL for an invalid timeout.
 */
static int
mq_block(message *m, struct mq *mq, int op)
{
	struct mqwait *w;
	clock_t uptime, realtime, ticks;
	time_t boottime;
	int64_t sec, now_ticks, deadline;
	long nsec;
	int r, hz;

	if (m->m_lc_ipc_mqio.flags & IPC_MQ_NONBLOCK)
		return EAGAIN;

	w = &mq_wait[_ENDPOINT_P(m->m_source)];
	ticks = 0;
	if (m->m_lc_ipc_mqio.flags & IPC_MQ_TIMED) {
		sec = m->m_lc_ipc_mqio.tv_sec;
		nsec = m->m_lc_ipc_mqio.tv_nsec;
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

	w->op = op;
	w->endpt = m->m_source;
	w->mq = mq;
	w->buf = m->m_lc_ipc_mqio.buf;
	w->len = m->m_lc_ipc_mqio.len;
	w->prio = m->m_lc_ipc_mqio.prio;
	w->seq = ++mq_seq;
	w->timed = (ticks > 0);
	if (w->timed)
		set_timer(&w->tmr, ticks, mq_timeout,
		    (int)_ENDPOINT_P(m->m_source));
	mq_update_sub();
	return SUSPEND;
}

static struct mq *
mq_lookup(message *m)
{

	return mq_find(m->m_lc_ipc_mqio.dev, m->m_lc_ipc_mqio.ino);
}

static void
mq_attr_reply(message *m, struct mq *mq)
{

	m->m_ipc_lc_mq.maxmsg = mq->maxmsg;
	m->m_ipc_lc_mq.msgsize = mq->msgsize;
	m->m_ipc_lc_mq.curmsgs = mq->curmsgs;
}

int
do_mq_open(message *m)
{
	struct mq *mq;
	long maxmsg, msgsize;
	size_t bytes;
	int i, flags;

	mq_init();
	flags = m->m_lc_ipc_mqopen.flags;
	mq = mq_find(m->m_lc_ipc_mqopen.dev, m->m_lc_ipc_mqopen.ino);

	/* A just-created token file whose key we know is a reused inode: the
	 * old queue's token is gone (unlinked, or removed behind our back).
	 */
	if (mq != NULL && (flags & IPC_MQ_FRESH)) {
		if (mq_has_waiters(mq))
			return EBUSY;		/* cannot happen with a fresh file */
		mq_free(mq);
		mq = NULL;
	}
	if (mq != NULL) {
		mq_attr_reply(m, mq);
		return OK;
	}
	if (!(flags & IPC_MQ_CREAT))
		return ENOENT;

	maxmsg = m->m_lc_ipc_mqopen.maxmsg;
	msgsize = m->m_lc_ipc_mqopen.msgsize;
	if (maxmsg == 0 && msgsize == 0) {
		maxmsg = MQ_DEF_MAXMSG;
		msgsize = MQ_DEF_MSGSIZE;
	}
	if (maxmsg <= 0 || maxmsg > MQ_MAXMSG_MAX || msgsize <= 0 ||
	    msgsize > MQ_MSGSIZE_MAX ||
	    (size_t)maxmsg * msgsize > MQ_QUEUE_BYTES_MAX)
		return EINVAL;
	bytes = (size_t)maxmsg * msgsize;

	for (i = 0; i < MQ_MAX && mq_list[i].used; i++)
		;
	if (i == MQ_MAX || mq_reserved + bytes > MQ_TOTAL_BYTES_MAX) {
		/* Make room: drop unlinked queues nobody is blocked on. */
		for (i = 0; i < MQ_MAX; i++)
			if (mq_list[i].used && mq_list[i].unlinked &&
			    !mq_has_waiters(&mq_list[i]))
				mq_free(&mq_list[i]);
		for (i = 0; i < MQ_MAX && mq_list[i].used; i++)
			;
		if (i == MQ_MAX)
			return ENFILE;
		if (mq_reserved + bytes > MQ_TOTAL_BYTES_MAX)
			return ENOSPC;
	}

	mq = &mq_list[i];
	memset(mq, 0, sizeof(*mq));
	mq->used = 1;
	mq->dev = m->m_lc_ipc_mqopen.dev;
	mq->ino = m->m_lc_ipc_mqopen.ino;
	mq->maxmsg = maxmsg;
	mq->msgsize = msgsize;
	mq->nt_endpt = NONE;
	mq_reserved += bytes;
	mq_attr_reply(m, mq);
	return OK;
}

int
do_mq_send(message *m)
{
	struct mq *mq;
	int r;

	mq_init();
	if ((mq = mq_lookup(m)) == NULL)
		return EBADF;
	if (m->m_lc_ipc_mqio.len > (size_t)mq->msgsize)
		return EMSGSIZE;
	if (m->m_lc_ipc_mqio.prio >= MQ_PRIO_LIMIT)
		return EINVAL;
	if (mq->curmsgs >= mq->maxmsg)
		return mq_block(m, mq, OP_SEND);

	r = mq_enqueue(mq, m->m_source, m->m_lc_ipc_mqio.buf,
	    m->m_lc_ipc_mqio.len, m->m_lc_ipc_mqio.prio);
	if (r == OK)
		mq_run(mq);			/* a receiver may be waiting */
	return r;
}

int
do_mq_receive(message *m)
{
	struct mq *mq;
	ssize_t len;
	unsigned int prio;
	int r;

	mq_init();
	if ((mq = mq_lookup(m)) == NULL)
		return EBADF;
	if (m->m_lc_ipc_mqio.len < (size_t)mq->msgsize)
		return EMSGSIZE;
	if (mq->curmsgs == 0)
		return mq_block(m, mq, OP_RECEIVE);

	r = mq_dequeue(mq, m->m_source, m->m_lc_ipc_mqio.buf, &len, &prio);
	if (r != OK)
		return r;
	m->m_ipc_lc_mq.len = len;
	m->m_ipc_lc_mq.prio = prio;
	mq_run(mq);				/* a sender may be waiting */
	return OK;
}

int
do_mq_getattr(message *m)
{
	struct mq *mq;

	mq_init();
	if ((mq = mq_lookup(m)) == NULL)
		return EBADF;
	mq_attr_reply(m, mq);
	return OK;
}

int
do_mq_notify(message *m)
{
	struct mq *mq;

	mq_init();
	if ((mq = mq_lookup(m)) == NULL)
		return EBADF;

	if (!(m->m_lc_ipc_mqio.flags & IPC_MQ_REGISTER)) {
		/* Remove the caller's registration, if it has one. */
		if (mq->nt_endpt == m->m_source)
			mq->nt_endpt = NONE;
		mq_update_sub();
		return OK;
	}

	if (mq->nt_endpt != NONE)
		return EBUSY;
	mq->nt_endpt = m->m_source;
	mq->nt_pid = (pid_t)m->m_lc_ipc_mqio.len;	/* the caller's pid */
	mq->nt_signo = m->m_lc_ipc_mqio.prio;		/* 0: SIGEV_NONE */
	mq->nt_value = m->m_lc_ipc_mqio.buf;
	mq_update_sub();
	return OK;
}

int
do_mq_unlink(message *m)
{
	struct mq *mq;

	mq_init();
	if ((mq = mq_lookup(m)) != NULL)
		mq->unlinked = 1;	/* lives on for open descriptors */
	return OK;
}

/*
 * PM told us about a process being signaled or exiting.  A blocked send or
 * receive is cancelled: with EINTR for a signal; without reply for an exit,
 * which also drops the process's notification registrations.
 */
void
mq_process_event(endpoint_t endpt, int has_exited)
{
	struct mqwait *w;
	unsigned int slot;
	int i;

	slot = _ENDPOINT_P(endpt);
	if (slot >= NR_PROCS)
		return;
	w = &mq_wait[slot];
	if (w->op != OP_NONE && w->endpt == endpt) {
		if (has_exited) {
			if (w->timed)
				cancel_timer(&w->tmr);
			w->op = OP_NONE;
			w->mq = NULL;
		} else
			mq_reply(w, EINTR, 0, 0);
	}
	if (has_exited)
		for (i = 0; i < MQ_MAX; i++)
			if (mq_list[i].used && mq_list[i].nt_endpt == endpt)
				mq_list[i].nt_endpt = NONE;
	mq_update_sub();
}
