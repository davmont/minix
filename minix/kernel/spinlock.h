#ifndef __SPINLOCK_H__
#define __SPINLOCK_H__

#include <sys/types.h>
#include <archtypes.h>

#ifdef __kernel__
#include "config.h"
#endif

typedef struct spinlock {
	atomic_t val;
} spinlock_t;

#ifndef CONFIG_SMP

#define SPINLOCK_DEFINE(name)
#define PRIVATE_SPINLOCK_DEFINE(name)
#define SPINLOCK_DECLARE(name)
#define spinlock_init(sl)
#define spinlock_lock(sl)
#define spinlock_unlock(sl)

#else

/* SMP */
#define SPINLOCK_DEFINE(name)	spinlock_t name;
#define PRIVATE_SPINLOCK_DEFINE(name)	PRIVATE SPINLOCK_DEFINE(name)
#define SPINLOCK_DECLARE(name)	extern SPINLOCK_DEFINE(name)
#define spinlock_init(sl) do { (sl)->val = 0; } while (0)

#if CONFIG_MAX_CPUS == 1
#define spinlock_lock(sl)
#define spinlock_unlock(sl)
#else
void arch_spinlock_lock(atomic_t * sl);
void arch_spinlock_unlock(atomic_t * sl);
#define spinlock_lock(sl)	arch_spinlock_lock((atomic_t*) sl)
#define spinlock_unlock(sl)	arch_spinlock_unlock((atomic_t*) sl)
#endif


#endif /* CONFIG_SMP */

/*
 * BKL with per-CPU "I hold it" tracking.  BKL_UNLOCK becomes a no-op
 * when the calling CPU's flag is clear, so the nested-IPI path can
 * skip BKL_LOCK without the trailing context_stop(KERNEL) BKL_UNLOCK
 * releasing a lock the CPU never acquired (which would corrupt BSP's
 * critical section).
 */
#if defined(CONFIG_SMP) && CONFIG_MAX_CPUS > 1
#define BKL_LOCK() do {							\
	spinlock_lock(&big_kernel_lock);				\
	bkl_held_by_cpu[cpuid] = 1;					\
} while (0)
#define BKL_UNLOCK() do {						\
	if (bkl_held_by_cpu[cpuid]) {					\
		bkl_held_by_cpu[cpuid] = 0;				\
		spinlock_unlock(&big_kernel_lock);			\
	}								\
} while (0)
#else
#define BKL_LOCK()	spinlock_lock(&big_kernel_lock)
#define BKL_UNLOCK()	spinlock_unlock(&big_kernel_lock)
#endif

#endif /* __SPINLOCK_H__ */
