#define _MINIX_SYSTEM

#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>
#include <minix/param.h>

#include <stdint.h>
#include <time.h>

/*
 * CLOCK_REALTIME and CLOCK_MONOTONIC to the nanosecond, with no call into PM.
 *
 * The kernel keeps both clocks in ticks, in a page every process has mapped
 * (struct kclockinfo), and with them the TSC at the last tick.  Between ticks
 * the time is the last tick's plus what the TSC has counted since, up to the
 * next tick: so the clock never runs back when the tick comes, even while
 * adjtime() runs realtime fast or slow (rt_step).  The time at a tick is what
 * PM's clock_gettime() gives, from the same ticks.
 *
 * Only where the kernel has these fields (MINIX_KIF_CLOCKTSC: kclockinfo is
 * not userland ABI) and says the TSC may be used (tsc_hz); elsewhere this
 * returns -1, and the caller asks PM.
 */

extern struct minix_kerninfo *_minix_kerninfo;

#define NSEC	1000000000ULL

#if defined(__i386__) || defined(__x86_64__)
static inline uint64_t
rdtsc_ordered(void)
{
	uint32_t lo, hi;

	/* lfence: not before the loads of the clock page are done. */
	__asm __volatile("lfence; rdtsc" : "=a" (lo), "=d" (hi) :: "memory");
	return ((uint64_t) hi << 32) | lo;
}
#endif

int
__minix_clock_fast(clockid_t clock_id, struct timespec *ts)
{
#if defined(__i386__) || defined(__x86_64__)
	volatile struct kclockinfo *kc;
	uint64_t ticks, tsc, tick_tsc, tsc_hz, ns, in, cap;
	uint32_t seq, hz, step;
	time_t base;

	if (clock_id != CLOCK_REALTIME && clock_id != CLOCK_MONOTONIC)
		return -1;
	if (_minix_kerninfo == NULL ||
	    !(_minix_kerninfo->ki_flags & MINIX_KIF_CLOCKTSC) ||
	    (kc = _minix_kerninfo->kclockinfo) == NULL)
		return -1;

	do {
		while ((seq = kc->seq) & 1)
			;
		__insn_barrier();
		if ((tsc_hz = kc->tsc_hz) == 0)
			return -1;
		if ((hz = kc->hz) == 0)
			return -1;
		tick_tsc = kc->tick_tsc;
		if (clock_id == CLOCK_MONOTONIC) {
			ticks = kc->uptime;
			base = 0;
			step = 1;
		} else {
			ticks = kc->realtime;
			base = kc->boottime;
			step = kc->rt_step;
		}
		tsc = rdtsc_ordered();
		__insn_barrier();
	} while (kc->seq != seq);

	/* Since the last tick, short of the time at the next one. */
	cap = step * (NSEC / hz);
	if (cap > 0)
		cap--;
	if ((int64_t) (tsc - tick_tsc) <= 0)
		in = 0;
	else if (tsc - tick_tsc >= tsc_hz)
		in = cap;
	else
		in = (tsc - tick_tsc) * NSEC / tsc_hz;
	if (in > cap)
		in = cap;

	ns = (ticks % hz) * NSEC / hz + in;
	ts->tv_sec = base + (time_t) (ticks / hz) + (time_t) (ns / NSEC);
	ts->tv_nsec = (long) (ns % NSEC);
	return 0;
#else
	return -1;
#endif
}

/* The resolution __minix_clock_fast() reads 'clock_id' with; -1 if it does
 * not. */
int
__minix_clock_fast_res(clockid_t clock_id, struct timespec *ts)
{
	struct timespec now;

	if (__minix_clock_fast(clock_id, &now) != 0)
		return -1;
	ts->tv_sec = 0;
	ts->tv_nsec = 1;
	return 0;
}
