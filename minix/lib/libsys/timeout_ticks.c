#include "sysutil.h"

#include <minix/timers.h>
#include <stdint.h>

/*
 * Timers (set_timer(3)) count whole clock ticks, from the last tick, which
 * may be most of a tick ago.  These say how many a timer set now must count
 * to go off no sooner than a given time, and at the first tick that late,
 * not one more:
 *
 *   uptime_ns:		the monotonic time, in ns since boot
 *   deadline_ticks:	ticks until a monotonic time
 *   timeout_ticks:	ticks until a time from now
 *   tick_left_ns:	the time until a tick, in ns
 *
 * Tick k is at k / hz seconds of monotonic time, by definition.  Between
 * ticks, where userland reads the clock off the TSC (MINIX_KIF_CLOCKTSC, see
 * <minix/type.h> and libc's clock_fast.c), so do we.  Elsewhere we cannot
 * tell how far into the tick we are, and take the time as that of the last
 * tick; a timeout from now then counts one tick more, as NetBSD's tvtohz()
 * does.
 */

#define NSEC	1000000000ULL

#if defined(__i386__) || defined(__x86_64__)
static inline uint64_t
rdtsc_ordered(void)
{
	uint32_t lo, hi;

	__asm __volatile("lfence; rdtsc" : "=a" (lo), "=d" (hi) :: "memory");
	return ((uint64_t) hi << 32) | lo;
}
#endif

/* The last tick, and how far past it we are in ns; -1 if we cannot tell. */
static int64_t
into_tick(clock_t *tick)
{
#if defined(__i386__) || defined(__x86_64__)
	struct minix_kerninfo *ki;
	volatile struct kclockinfo *kc;
	uint64_t tsc, tick_tsc, tsc_hz, in;
	uint32_t seq, hz;
	clock_t uptime;

	ki = get_minix_kerninfo();
	if (!(ki->ki_flags & MINIX_KIF_CLOCKTSC) ||
	    (kc = ki->kclockinfo) == NULL)
		return -1;
	do {
		while ((seq = kc->seq) & 1)
			;
		__insn_barrier();
		tsc_hz = kc->tsc_hz;
		hz = kc->hz;
		tick_tsc = kc->tick_tsc;
		uptime = kc->uptime;
		tsc = rdtsc_ordered();
		__insn_barrier();
	} while (kc->seq != seq);
	if (tsc_hz == 0 || hz == 0)
		return -1;

	if ((int64_t) (tsc - tick_tsc) <= 0)
		in = 0;
	else if (tsc - tick_tsc >= tsc_hz)
		in = NSEC;
	else
		in = (tsc - tick_tsc) * NSEC / tsc_hz;
	if (in > NSEC / hz - 1)
		in = NSEC / hz - 1;	/* not into the next tick */
	*tick = uptime;
	return (int64_t) in;
#else
	return -1;
#endif
}

/* The time of tick 'k', rounded down. */
static uint64_t
tick_ns(uint64_t k, uint32_t hz)
{

	return (k / hz) * NSEC + (k % hz) * NSEC / hz;
}

/* The first tick at or after 'ns'. */
static uint64_t
ns_tick(uint64_t ns, uint32_t hz)
{

	return (ns / NSEC) * hz + ((ns % NSEC) * hz + NSEC - 1) / NSEC;
}

u64_t
uptime_ns(int *precise)
{
	clock_t tick;
	int64_t in;

	if ((in = into_tick(&tick)) < 0) {
		if (precise != NULL)
			*precise = FALSE;
		return tick_ns(getticks(), sys_hz());
	}
	if (precise != NULL)
		*precise = TRUE;
	return tick_ns(tick, sys_hz()) + (uint64_t) in;
}

clock_t
deadline_ticks(u64_t when)
{
	uint64_t target, now;

	target = ns_tick(when, sys_hz());
	now = getticks();
	if (target <= now)
		return 1;		/* gone by: at the next tick */
	if (target - now > TMRDIFF_MAX)
		return TMRDIFF_MAX;
	return (clock_t) (target - now);
}

clock_t
timeout_ticks(time_t sec, long nsec)
{
	uint32_t hz;
	uint64_t now, t;
	int precise;

	hz = sys_hz();
	if (sec < 0 || nsec < 0)
		return 1;
	if (sec >= TMRDIFF_MAX / hz - 2)
		return TMRDIFF_MAX;

	now = uptime_ns(&precise);
	if (precise)
		return deadline_ticks(now + (uint64_t) sec * NSEC +
		    (uint64_t) nsec);

	/* Rounded up, and one more. */
	t = (uint64_t) sec * hz + ((uint64_t) nsec * hz + NSEC - 1) / NSEC + 1;
	return (t > TMRDIFF_MAX) ? TMRDIFF_MAX : (clock_t) t;
}

u64_t
tick_left_ns(clock_t tick)
{
	clock_t now;
	int64_t in;
	int32_t d;

	if ((in = into_tick(&now)) < 0) {
		now = getticks();
		in = 0;
	}
	if ((d = (int32_t) (uint32_t) (tick - now)) <= 0)
		return 0;		/* come */
	return tick_ns((uint64_t) d, sys_hz()) - (uint64_t) in;
}
