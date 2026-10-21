/* The kernel call implemented in this file:
 *   m_type:	SYS_SETTIME
 *
 * The parameters for this kernel call are:
 *   m_lsys_krn_sys_settime.now
 *   m_lsys_krn_sys_settime.clock_id
 *   m_lsys_krn_sys_settime.sec
 *   m_lsys_krn_sys_settime.nsec
 */

#include "kernel/system.h"
#include <minix/endpoint.h>
#include <time.h>

/*===========================================================================*
 *				do_settime				     *
 *===========================================================================*/
int do_settime(struct proc * caller, message * m_ptr)
{
  clock_t newclock;
  int32_t ticks;
  time_t boottime, timediff, timediff_ticks;

  /* only realtime can change */
  if (m_ptr->m_lsys_krn_sys_settime.clock_id != CLOCK_REALTIME)
	return EINVAL;

  /* user just wants to adjtime() */
  if (m_ptr->m_lsys_krn_sys_settime.now == 0) {
	/* convert delta value from seconds and nseconds to ticks */
	ticks = (m_ptr->m_lsys_krn_sys_settime.sec * system_hz) +
		(m_ptr->m_lsys_krn_sys_settime.nsec/(1000000000/system_hz));
	set_adjtime_delta(ticks);
	return(OK);
  } /* else user wants to set the time */

  boottime = get_boottime();

  timediff = m_ptr->m_lsys_krn_sys_settime.sec - boottime;
  timediff_ticks = timediff * system_hz;

  /* Prevent a negative value for realtime, or one that a clock_t cannot
   * hold: it is 32 bits wide, so setting the clock years ahead of boottime
   * (back again after setting it far into the past, say) would truncate it.
   * The bound used to be LONG_MAX/2, which only did this where long is
   * 32 bits.
   */
  if (m_ptr->m_lsys_krn_sys_settime.sec <= boottime ||
      timediff_ticks > (time_t)((clock_t)-1 / 2)) {
  	/* boottime was likely wrong, try to correct it. */
	set_boottime(m_ptr->m_lsys_krn_sys_settime.sec);
	set_realtime(1);
	return(OK);
  }

  /* calculate the new value of realtime in ticks */
  newclock = timediff_ticks +
      (m_ptr->m_lsys_krn_sys_settime.nsec/(1000000000/system_hz));

  set_realtime(newclock);

  return(OK);
}
