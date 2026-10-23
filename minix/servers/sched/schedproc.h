/* This table has one slot per process.  It contains scheduling information
 * for each process.
 */
#include <limits.h>

#include <minix/bitmap.h>
#include <sys/sched.h>

/* EXTERN should be extern except in main.c, where we want to keep the struct */
#ifdef _MAIN
#undef EXTERN
#define EXTERN
#endif

#ifndef CONFIG_SMP
#define CONFIG_MAX_CPUS 1
#endif

/**
 * We might later want to add more information to this table, such as the
 * process owner, process group or cpumask.
 */

EXTERN struct schedproc {
	endpoint_t endpoint;	/* process endpoint id */
	endpoint_t parent;	/* parent endpoint id */
	unsigned flags;		/* flag bits */

	/* User space scheduling */
	unsigned max_priority;	/* this process' highest allowed priority */
	unsigned priority;		/* the process' current priority */
	unsigned time_slice;		/* this process's time slice */
	unsigned cpu;		/* what CPU is the process running on */
	bitchunk_t cpu_mask[BITMAP_CHUNKS(CONFIG_MAX_CPUS)]; /* what CPUs is the
								process allowed
								to run on */

	/* POSIX scheduling policy, sched_setscheduler(2) */
	int policy;		/* SCHED_OTHER, SCHED_FIFO or SCHED_RR */
	int rt_prio;		/* its priority, for SCHED_FIFO and SCHED_RR */
	clock_t rt_time;	/* CPU time (ticks) at the last real-time sample */
} schedproc[NR_PROCS];

/* Flag values */
#define IN_USE		0x00001	/* set when 'schedproc' slot in use */
#define RT_THROTTLED	0x00002	/* real-time, run as time-sharing for now */

#define is_rt(p)	((p)->policy != SCHED_OTHER)
