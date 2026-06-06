/*
 * pthread_glue.c — MINIX libc support for libpthread.
 *
 * libpthread references a number of NetBSD primitives MINIX libc does not yet
 * implement.  This file provides them: NetBSD-internal LWP and scheduler calls
 * as stubs, _sys_<call> raw-syscall variants as thin wrappers over the normal
 * libc entry points, and a few public POSIX functions MINIX libc is missing.
 *
 * NOT covered here: _lwp_wait(2) (real PM call, see _lwp_wait.c) and per-thread
 * signals/_lwp_kill (lwp_stubs.c).
 *
 * TODO: replace the best-effort public stubs (mprotect, sched_yield) and the
 * _sched_* stubs with real implementations.
 */
#include <sys/cdefs.h>
/*
 * NB: intentionally no "namespace.h" — these definitions must export the public
 * symbol names (sched_yield, pthread_atfork, mprotect, ...) that libpthread
 * references; namespace.h would rename some of them to their _-prefixed forms.
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <lib.h>
#include <minix/callnr.h>
#include <lwp.h>
#include <sys/lwpctl.h>
#include <sched.h>
#include <signal.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

/* --- NetBSD-internal LWP control: not yet supported on MINIX. --- */
int _lwp_suspend(lwpid_t l)			{ (void)l; errno = ENOSYS; return -1; }
int _lwp_continue(lwpid_t l)			{ (void)l; errno = ENOSYS; return -1; }
int _lwp_wakeup(lwpid_t l)			{ (void)l; errno = ENOSYS; return -1; }
int _lwp_setname(lwpid_t l, const char *n)	{ (void)l; (void)n; return 0; }

/*
 * _lwp_ctl: hand libpthread a valid lwpctl page so it can read lc_curcpu.
 * We have no kernel-maintained per-CPU state, so we point every caller at a
 * single shared, immutable struct reporting LWPCTL_CPU_NONE.  That value tells
 * the mutex/rwlock spin paths "the holder is not currently on a CPU", so they
 * skip the spin-while-running optimization and park immediately -- correct,
 * just without the adaptive-spin speedup.  Returning failure here is not an
 * option: pthread__init() treats it as fatal (err(3)).
 */
static lwpctl_t __lwpctl_shared = { .lc_curcpu = LWPCTL_CPU_NONE, .lc_pctr = 0 };
int _lwp_ctl(int f, struct lwpctl **a)
{
	(void)f;
	if (a != NULL)
		*a = &__lwpctl_shared;
	return 0;
}

/*
 * Detach: in our model a thread is created detached or joinable via the
 * _lwp_create() flags; runtime re-detach is a no-op for now (the thread will be
 * reaped on exit if detached, else it waits for a join).
 */
int _lwp_detach(lwpid_t l)			{ (void)l; return 0; }

/* --- Raw scheduler syscalls: stubbed (default priority, no affinity). --- */
int
_sched_setparam(pid_t pid, lwpid_t lid, int policy, const struct sched_param *p)
{
	(void)pid; (void)lid; (void)policy; (void)p;
	return 0;	/* accept; threads run at the default priority */
}

int
_sched_getparam(pid_t pid, lwpid_t lid, int *policy, struct sched_param *p)
{
	(void)pid; (void)lid;
	if (policy != NULL)
		*policy = SCHED_OTHER;
	if (p != NULL)
		memset(p, 0, sizeof(*p));
	return 0;
}

int
_sched_setaffinity(pid_t pid, lwpid_t lid, size_t sz, const cpuset_t *s)
{
	(void)pid; (void)lid; (void)sz; (void)s;
	errno = ENOSYS;
	return -1;
}

int
_sched_getaffinity(pid_t pid, lwpid_t lid, size_t sz, cpuset_t *s)
{
	(void)pid; (void)lid; (void)sz; (void)s;
	errno = ENOSYS;
	return -1;
}

/* --- Public POSIX functions MINIX libc is missing. --- */

/* sched_yield(2): advisory; a plain return is POSIX-conformant.
 * <sched.h> macro-renames sched_yield -> __libc_thr_yield, so #undef it to
 * export the real public symbol. */
#undef sched_yield
int
sched_yield(void)
{
	return 0;
}

/*
 * mprotect(2): MINIX has no page-protection-change call yet.  libpthread uses
 * it only to install stack guard pages and treats failure as fatal to thread
 * creation, so report success (the guard simply is not enforced).
 * TODO: real protection via VM.
 */
int
mprotect(void *addr, size_t len, int prot)
{
	(void)addr; (void)len; (void)prot;
	return 0;
}

/* clock_nanosleep(2): implement via nanosleep(2) (TIMER_ABSTIME -> relative). */
int
clock_nanosleep(clockid_t clock_id, int flags, const struct timespec *rqtp,
    struct timespec *rmtp)
{
	struct timespec rel;

	if (flags & TIMER_ABSTIME) {
		struct timespec now;
		if (clock_gettime(clock_id, &now) == -1)
			return errno;
		rel.tv_sec = rqtp->tv_sec - now.tv_sec;
		rel.tv_nsec = rqtp->tv_nsec - now.tv_nsec;
		if (rel.tv_nsec < 0) { rel.tv_sec--; rel.tv_nsec += 1000000000L; }
		if (rel.tv_sec < 0)
			return 0;	/* deadline already passed */
		rqtp = &rel;
		rmtp = NULL;		/* remaining time is meaningless for ABSTIME */
	}
	if (nanosleep(rqtp, rmtp) == -1)
		return errno;
	return 0;
}

/* rasctl(2): restartable atomic sequences are NetBSD-specific; unsupported. */
int
rasctl(void *addr, size_t len, int op)
{
	(void)addr; (void)len; (void)op;
	errno = ENOSYS;
	return -1;
}

/*
 * pthread_atfork(3): minimal — MINIX does not run registered fork handlers yet.
 * (No threaded fork() use in the base system.)
 */
int
pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void))
{
	(void)prepare; (void)parent; (void)child;
	return 0;
}

/* --- _sys_<call>: raw (non-cancellable) syscall variants libpthread calls
 *     directly.  MINIX libc has no _sys_ layer, so map them to the normal
 *     entry points. --- */
ssize_t
_sys_write(int fd, const void *buf, size_t n)
{
	return write(fd, buf, n);
}

/*
 * Raw sigprocmask: issue the PM syscall directly.  We must NOT call the public
 * sigprocmask()/__sigprocmask14(): libpthread strong-aliases __sigprocmask14 to
 * pthread_sigmask (== __libc_thr_sigsetmask), which itself calls back into
 * _sys___sigprocmask14 -- forwarding to the public name would recurse forever.
 */
int
_sys___sigprocmask14(int how, const sigset_t *set, sigset_t *oset)
{
	message m;

	memset(&m, 0, sizeof(m));
	if (set == NULL) {
		m.m_lc_pm_sigset.how = SIG_INQUIRE;
		sigemptyset(&m.m_lc_pm_sigset.set);
	} else {
		m.m_lc_pm_sigset.how = how;
		m.m_lc_pm_sigset.set = *set;
	}
	if (_syscall(PM_PROC_NR, PM_SIGPROCMASK, &m) < 0)
		return -1;
	if (oset != NULL)
		*oset = m.m_pm_lc_sigset.set;
	return m.m_type;
}

/*
 * Raw setcontext: call libc's strong _setcontext.  The public setcontext is a
 * weak alias overridden by libpthread's pthread_setcontext, which calls back
 * into _sys_setcontext -- so forwarding to the public name would recurse.
 */
extern int _setcontext(const ucontext_t *);
int
_sys_setcontext(const ucontext_t *ucp)
{
	return _setcontext(ucp);
}

int
_sys_sched_yield(void)
{
	return sched_yield();
}
