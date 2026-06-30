/* Helper functions that allow driver writers to easily busy-wait (spin) for a
 * condition to become satisfied within a certain maximum time span.
 */
/* This implementation first spins without making any system calls for a
 * while, and then starts using system calls (specifically, the system call to
 * obtain the current time) while spinning. The reason for this is that in
 * many cases, the condition to be checked will become satisfied rather
 * quickly, and we want to avoid getting descheduled in that case. However,
 * after a while, running out of scheduling quantum will cause our priority to
 * be lowered, and we can avoid this by voluntarily giving up the CPU, by
 * making a system call.
 */
#include "sysutil.h"
#include <minix/spin.h>
#include <minix/minlib.h>

/* Number of microseconds to keep spinning initially, without performing a
 * system call. We pick a value somewhat smaller than a typical clock tick.
 * Note that for the above reasons, we want to avoid using sys_hz() here.
 */
#define TSC_SPIN		1000		/* in microseconds */

/* Internal spin states. */
enum {
	STATE_INIT,		/* simply check the condition (once) */
	STATE_BASE_TS,		/* get the initial TSC value (once) */
	STATE_TS,		/* use the TSC to spin (up to TSC_SPIN us) */
	STATE_UPTIME		/* use the clock to spin */
};

void spin_init(spin_t *s, u32_t usecs)
{
	/* Initialize the given spin state structure, set to spin at most the
	 * given number of microseconds.
	 */
	s->s_state = STATE_INIT;
	s->s_usecs = usecs;
	s->s_timeout = FALSE;
}

int spin_check(spin_t *s)
{
	/* Check whether a timeout has taken place. Return TRUE if the caller
	 * should continue spinning, and FALSE if a timeout has occurred. The
	 * implementation assumes that it is okay to spin a little bit too long
	 * (up to a full clock tick extra).
	 */
	u64_t cur_tsc;
	clock_t micro_delta;

	switch (s->s_state) {
	case STATE_INIT:
		s->s_state = STATE_BASE_TS;
		break;

	case STATE_BASE_TS:
		s->s_state = STATE_TS;
		read_tsc_64(&s->s_base_tsc);
		break;

	case STATE_TS:
		read_tsc_64(&cur_tsc);

		micro_delta = tsc_64_to_micros(cur_tsc - s->s_base_tsc);

		if (micro_delta >= s->s_usecs) {
			s->s_timeout = TRUE;
			return FALSE;
		}

		/* After the initial pure-spin window, start giving up the CPU
		 * between checks (see STATE_UPTIME).  We keep timing with the
		 * TSC, so s_base_tsc and s_usecs are left untouched. */
		if (micro_delta >= TSC_SPIN)
			s->s_state = STATE_UPTIME;

		break;

	case STATE_UPTIME:
		/*
		 * Continue timing the spin with the TSC, which always advances,
		 * rather than the clock tick reported by getticks().  The tick
		 * can stall -- most importantly, a driver busy-spinning right
		 * here keeps the clock interrupt from being serviced, so a
		 * getticks()-based timeout would never expire and the spin (and
		 * thus the whole system) would hang.  This manifested as a
		 * deterministic APIC-mode boot wedge: at_wini's w_waitfor() spun
		 * forever on a frozen tick.  We still call getticks() for its
		 * scheduling side effect (a kernel call that yields the CPU, so
		 * a long spin is not penalised), but ignore its value here.
		 */
		(void) getticks();

		read_tsc_64(&cur_tsc);

		micro_delta = tsc_64_to_micros(cur_tsc - s->s_base_tsc);

		if (micro_delta >= s->s_usecs) {
			s->s_timeout = TRUE;
			return FALSE;
		}

		break;

	default:
		panic("spin_check: invalid state %d", s->s_state);
	}

	return TRUE;
}
