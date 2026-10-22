/*	$NetBSD$	*/

/*-
 * MINIX POSIX 1003.1b semaphores.
 *
 * NetBSD's librt/sem.c builds POSIX semaphores on the _ksem_* kernel syscalls,
 * which MINIX does not provide.  A process-private unnamed semaphore is kept
 * here, in userland, on libpthread's mutex and condition variable, which work
 * over PM's _lwp_park/_lwp_unpark (as GNU Hurd layers its semaphores over a
 * kernel wait primitive).
 *
 * Named semaphores (sem_open and co.) and process-shared unnamed ones (sem_init
 * with a nonzero pshared) live in the IPC server (minix/servers/ipc/psem.c).
 * Their sem_t holds the server's id, tagged with the low bit, which a pointer
 * to our structure never has: so a shared one works in any process that sees
 * it, through fork() or shared memory.  sem_open() gives a process the same
 * sem_t for the same name until sem_close(), as POSIX has it.
 */

#include <sys/cdefs.h>
#include <sys/stat.h>
#include <lib.h>
#include <minix/rs.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define	SEM_MAGIC	0x90af5b3eU

struct _sem_st {
	unsigned int	sem_magic;
	pthread_mutex_t	sem_lock;
	pthread_cond_t	sem_cond;
	unsigned int	sem_count;	/* current value */
	unsigned int	sem_nwaiters;	/* threads blocked in sem_wait */
};

/* A semaphore of the IPC server: its id, tagged. */
#define	SEM_IS_SHARED(s)	(((uintptr_t)(s) & 1) != 0)
#define	SEM_ID(s)		((int)((uintptr_t)(s) >> 1))
#define	SEM_SHARED(id)		((sem_t)(((uintptr_t)(id) << 1) | 1))

#define	SEM_DIR			"/var/sem/"

static int
psem_call(int call, message *m)
{
	endpoint_t pt;

	if (minix_rs_lookup("ipc", &pt) != 0) {
		errno = ENOSYS;
		return -1;
	}
	return _syscall(pt, call, m);
}

/* A call on the shared semaphore 'sem' (a sem_t that SEM_IS_SHARED). */
static int
psem_op(int call, sem_t sem, int flags, const struct timespec *abstime)
{
	message m;

	memset(&m, 0, sizeof(m));
	m.m_lc_ipc_psem.id = SEM_ID(sem);
	m.m_lc_ipc_psem.flags = flags;
	if (abstime != NULL) {
		m.m_lc_ipc_psem.tv_sec = abstime->tv_sec;
		m.m_lc_ipc_psem.tv_nsec = (int32_t)abstime->tv_nsec;
		if (abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000L)
			m.m_lc_ipc_psem.tv_nsec = -1;	/* EINVAL if it blocks */
	}
	return psem_call(call, &m);
}

/*
 * The named semaphores this process has open: sem_open() returns the same
 * sem_t for the same semaphore until as many sem_close().
 */
struct named_sem {
	struct named_sem *next;
	sem_t		sem;		/* what sem_open() returns the address of */
	int		refs;
};
static struct named_sem *named_sems;
static pthread_mutex_t named_lock = PTHREAD_MUTEX_INITIALIZER;

static int
sem_valid(sem_t *sem)
{
	if (sem == NULL || *sem == NULL || (*sem)->sem_magic != SEM_MAGIC) {
		errno = EINVAL;
		return 0;
	}
	return 1;
}

int
sem_init(sem_t *sem, int pshared, unsigned int value)
{
	struct _sem_st *s;

	if (value > SEM_VALUE_MAX) {
		errno = EINVAL;
		return -1;
	}
	if (pshared != 0) {
		message m;
		int id;

		memset(&m, 0, sizeof(m));
		m.m_lc_ipc_psem.value = value;
		if ((id = psem_call(IPC_PSEM_INIT, &m)) < 0)
			return -1;
		*sem = SEM_SHARED(id);
		return 0;
	}

	s = calloc(1, sizeof(*s));
	if (s == NULL) {
		errno = ENOSPC;
		return -1;
	}

	(void)pthread_mutex_init(&s->sem_lock, NULL);
	(void)pthread_cond_init(&s->sem_cond, NULL);
	s->sem_count = value;
	s->sem_nwaiters = 0;
	s->sem_magic = SEM_MAGIC;

	*sem = s;
	return 0;
}

int
sem_destroy(sem_t *sem)
{
	struct _sem_st *s;

	if (sem != NULL && SEM_IS_SHARED(*sem))
		return psem_op(IPC_PSEM_DESTROY, *sem, 0, NULL) < 0 ? -1 : 0;
	if (!sem_valid(sem))
		return -1;

	s = *sem;
	(void)pthread_mutex_lock(&s->sem_lock);
	if (s->sem_nwaiters != 0) {
		(void)pthread_mutex_unlock(&s->sem_lock);
		errno = EBUSY;
		return -1;
	}
	s->sem_magic = 0;
	(void)pthread_mutex_unlock(&s->sem_lock);

	(void)pthread_cond_destroy(&s->sem_cond);
	(void)pthread_mutex_destroy(&s->sem_lock);
	free(s);
	*sem = NULL;
	return 0;
}

int
sem_wait(sem_t *sem)
{
	struct _sem_st *s;

	if (sem != NULL && SEM_IS_SHARED(*sem))
		return psem_op(IPC_PSEM_WAIT, *sem, 0, NULL) < 0 ? -1 : 0;
	if (!sem_valid(sem))
		return -1;

	s = *sem;
	(void)pthread_mutex_lock(&s->sem_lock);
	while (s->sem_count == 0) {
		s->sem_nwaiters++;
		(void)pthread_cond_wait(&s->sem_cond, &s->sem_lock);
		s->sem_nwaiters--;
	}
	s->sem_count--;
	(void)pthread_mutex_unlock(&s->sem_lock);
	return 0;
}

int
sem_trywait(sem_t *sem)
{
	struct _sem_st *s;
	int rv = 0;

	if (sem != NULL && SEM_IS_SHARED(*sem))
		return psem_op(IPC_PSEM_WAIT, *sem, IPC_PSEM_TRY, NULL) < 0 ?
		    -1 : 0;
	if (!sem_valid(sem))
		return -1;

	s = *sem;
	(void)pthread_mutex_lock(&s->sem_lock);
	if (s->sem_count == 0) {
		errno = EAGAIN;
		rv = -1;
	} else {
		s->sem_count--;
	}
	(void)pthread_mutex_unlock(&s->sem_lock);
	return rv;
}

int
sem_timedwait(sem_t *sem, const struct timespec * __restrict abstime)
{
	struct _sem_st *s;
	int error = 0;

	if (sem != NULL && SEM_IS_SHARED(*sem)) {
		if (abstime == NULL) {
			errno = EINVAL;
			return -1;
		}
		return psem_op(IPC_PSEM_WAIT, *sem, IPC_PSEM_TIMED, abstime) <
		    0 ? -1 : 0;
	}
	if (!sem_valid(sem))
		return -1;

	s = *sem;
	(void)pthread_mutex_lock(&s->sem_lock);
	while (s->sem_count == 0 && error == 0) {
		s->sem_nwaiters++;
		error = pthread_cond_timedwait(&s->sem_cond, &s->sem_lock,
		    abstime);
		s->sem_nwaiters--;
	}
	if (error == 0) {
		s->sem_count--;
	}
	(void)pthread_mutex_unlock(&s->sem_lock);
	if (error != 0) {
		errno = error;	/* ETIMEDOUT or EINVAL from cond_timedwait */
		return -1;
	}
	return 0;
}

int
sem_post(sem_t *sem)
{
	struct _sem_st *s;

	if (sem != NULL && SEM_IS_SHARED(*sem))
		return psem_op(IPC_PSEM_POST, *sem, 0, NULL) < 0 ? -1 : 0;
	if (!sem_valid(sem))
		return -1;

	s = *sem;
	(void)pthread_mutex_lock(&s->sem_lock);
	if (s->sem_count == SEM_VALUE_MAX) {
		(void)pthread_mutex_unlock(&s->sem_lock);
		errno = EOVERFLOW;
		return -1;
	}
	s->sem_count++;
	(void)pthread_cond_signal(&s->sem_cond);
	(void)pthread_mutex_unlock(&s->sem_lock);
	return 0;
}

int
sem_getvalue(sem_t * __restrict sem, int * __restrict sval)
{
	struct _sem_st *s;
	int v;

	if (sem != NULL && SEM_IS_SHARED(*sem)) {
		if ((v = psem_op(IPC_PSEM_GETVALUE, *sem, 0, NULL)) < 0)
			return -1;
		*sval = v;
		return 0;
	}
	if (!sem_valid(sem))
		return -1;

	s = *sem;
	(void)pthread_mutex_lock(&s->sem_lock);
	*sval = (int)s->sem_count;
	(void)pthread_mutex_unlock(&s->sem_lock);
	return 0;
}

/*
 * Map a semaphore name to its token file, as mq_open() does: a leading slash
 * and no other (EINVAL otherwise, as on NetBSD).  The name, slash included,
 * is at most NAME_MAX long.
 */
static int
sem_path(const char *name, char *buf, size_t bufsz)
{

	if (name == NULL || name[0] != '/' || name[1] == '\0' ||
	    strchr(name + 1, '/') != NULL) {
		errno = EINVAL;
		return -1;
	}
	if (strlen(name) > NAME_MAX ||
	    (size_t)snprintf(buf, bufsz, "%s%s", SEM_DIR, name + 1) >= bufsz) {
		errno = ENAMETOOLONG;
		return -1;
	}
	return 0;
}

sem_t *
sem_open(const char *name, int oflag, ...)
{
	char path[PATH_MAX];
	struct named_sem *ns;
	struct stat st;
	message m;
	va_list ap;
	mode_t mode = 0;
	unsigned int value = 0;
	int fd = -1, fresh = 0, id, err;

	if (sem_path(name, path, sizeof(path)) < 0)
		return SEM_FAILED;
	if (oflag & O_CREAT) {
		va_start(ap, oflag);
		mode = (mode_t)va_arg(ap, int);
		value = va_arg(ap, unsigned int);
		va_end(ap);
		if (value > INT_MAX) {
			errno = EINVAL;
			return SEM_FAILED;
		}
		/* Know whether we created the token: a new file may have the
		 * inode number of an unlinked semaphore's. */
		fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, mode);
		if (fd >= 0)
			fresh = 1;
		else if (errno != EEXIST || (oflag & O_EXCL))
			return SEM_FAILED;
	}
	/* POSIX: read and write permission are needed to use one. */
	if (fd < 0 && (fd = open(path, O_RDWR | O_CLOEXEC)) < 0)
		return SEM_FAILED;
	if (fstat(fd, &st) != 0)
		goto fail;

	memset(&m, 0, sizeof(m));
	m.m_lc_ipc_psem.dev = st.st_dev;
	m.m_lc_ipc_psem.ino = st.st_ino;
	m.m_lc_ipc_psem.value = value;
	m.m_lc_ipc_psem.flags = (fresh ? IPC_PSEM_FRESH : 0) |
	    ((oflag & O_CREAT) ? IPC_PSEM_CREAT : 0);
	if ((id = psem_call(IPC_PSEM_OPEN, &m)) < 0)
		goto fail;
	(void)close(fd);

	(void)pthread_mutex_lock(&named_lock);
	for (ns = named_sems; ns != NULL; ns = ns->next)
		if (ns->sem == SEM_SHARED(id))
			break;
	if (ns == NULL) {
		if ((ns = calloc(1, sizeof(*ns))) == NULL) {
			(void)pthread_mutex_unlock(&named_lock);
			errno = ENOSPC;
			return SEM_FAILED;
		}
		ns->sem = SEM_SHARED(id);
		ns->next = named_sems;
		named_sems = ns;
	}
	ns->refs++;
	(void)pthread_mutex_unlock(&named_lock);
	return &ns->sem;

fail:
	err = errno;
	(void)close(fd);
	if (fresh)
		(void)unlink(path);
	errno = err;
	return SEM_FAILED;
}

int
sem_close(sem_t *sem)
{
	struct named_sem *ns, **pp;

	(void)pthread_mutex_lock(&named_lock);
	for (pp = &named_sems; (ns = *pp) != NULL; pp = &ns->next)
		if (&ns->sem == sem)
			break;
	if (ns == NULL) {
		(void)pthread_mutex_unlock(&named_lock);
		errno = EINVAL;
		return -1;
	}
	if (--ns->refs == 0) {
		*pp = ns->next;
		free(ns);
	}
	(void)pthread_mutex_unlock(&named_lock);
	return 0;
}

int
sem_unlink(const char *name)
{
	char path[PATH_MAX];
	struct stat st;
	message m;

	if (sem_path(name, path, sizeof(path)) < 0)
		return -1;
	if (stat(path, &st) != 0)
		return -1;			/* ENOENT, EACCES */
	if (unlink(path) != 0)
		return -1;
	memset(&m, 0, sizeof(m));
	m.m_lc_ipc_psem.dev = st.st_dev;
	m.m_lc_ipc_psem.ino = st.st_ino;
	(void)psem_call(IPC_PSEM_UNLINK, &m);
	return 0;
}
