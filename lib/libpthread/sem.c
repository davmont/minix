/*	$NetBSD$	*/

/*-
 * MINIX POSIX 1003.1b unnamed-semaphore implementation.
 *
 * NetBSD's librt/sem.c builds POSIX semaphores on the _ksem_* kernel syscalls,
 * which MINIX does not provide.  Instead we implement them entirely in userland
 * on top of libpthread's mutex + condition variable, which already work over the
 * PM-side _lwp_park/_lwp_unpark wait primitive.  This mirrors how GNU Hurd layers
 * its semaphores over a kernel wait primitive in userland.
 *
 * Only process-private (pshared == 0) unnamed semaphores are supported; named
 * semaphores (sem_open/sem_close/sem_unlink) and process-shared semaphores
 * return ENOSYS, as MINIX has no shared-memory object to back them.
 */

#include <sys/cdefs.h>

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

#define	SEM_MAGIC	0x90af5b3eU

struct _sem_st {
	unsigned int	sem_magic;
	pthread_mutex_t	sem_lock;
	pthread_cond_t	sem_cond;
	unsigned int	sem_count;	/* current value */
	unsigned int	sem_nwaiters;	/* threads blocked in sem_wait */
};

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
		/* No shared-memory backing for process-shared semaphores. */
		errno = ENOSYS;
		return -1;
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

	if (!sem_valid(sem))
		return -1;

	s = *sem;
	(void)pthread_mutex_lock(&s->sem_lock);
	*sval = (int)s->sem_count;
	(void)pthread_mutex_unlock(&s->sem_lock);
	return 0;
}

/*
 * Named semaphores need a filesystem/shared-memory namespace MINIX lacks.
 */
sem_t *
sem_open(const char *name, int oflag, ...)
{
	(void)name;
	(void)oflag;
	errno = ENOSYS;
	return SEM_FAILED;
}

int
sem_close(sem_t *sem)
{
	(void)sem;
	errno = ENOSYS;
	return -1;
}

int
sem_unlink(const char *name)
{
	(void)name;
	errno = ENOSYS;
	return -1;
}
