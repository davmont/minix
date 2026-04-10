/*	$NetBSD: pthread_types.h stub for Minix amd64 cross-build	*/

#ifndef _PTHREAD_TYPES_H
#define _PTHREAD_TYPES_H

/*
 * Minimal stub for Minix cross-build: Minix does not support pthreads
 * directly, but sys/sys/types.h includes this header unconditionally
 * when POSIX feature macros are set. Provide opaque types so that
 * headers which forward-declare pthread interfaces compile cleanly.
 */

struct __pthread_st;
typedef struct __pthread_st *pthread_t;

struct __pthread_mutex_st;
typedef struct __pthread_mutex_st *pthread_mutex_t;

struct __pthread_cond_st;
typedef struct __pthread_cond_st *pthread_cond_t;

struct __pthread_rwlock_st;
typedef struct __pthread_rwlock_st *pthread_rwlock_t;

struct __pthread_attr_st;
typedef struct __pthread_attr_st *pthread_attr_t;

struct __pthread_mutexattr_st;
typedef struct __pthread_mutexattr_st *pthread_mutexattr_t;

struct __pthread_condattr_st;
typedef struct __pthread_condattr_st *pthread_condattr_t;

struct __pthread_rwlockattr_st;
typedef struct __pthread_rwlockattr_st *pthread_rwlockattr_t;

struct __pthread_barrier_st;
typedef struct __pthread_barrier_st *pthread_barrier_t;

struct __pthread_barrierattr_st;
typedef struct __pthread_barrierattr_st *pthread_barrierattr_t;

typedef unsigned int pthread_key_t;
typedef int pthread_once_t;
typedef volatile unsigned int pthread_spinlock_t;

#endif /* _PTHREAD_TYPES_H */
