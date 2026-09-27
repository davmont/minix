#ifndef __CLOCK_X86_64_H__
#define __CLOCK_X86_64_H__

int init_8253A_timer(unsigned freq);
void stop_8253A_timer(void);
int wait_for_pit_ticks(volatile unsigned *ticks, unsigned want,
	const char *who);
void arch_timer_int_handler(void);
void bkl_unlock_after_intr(void);

#endif /* __CLOCK_X86_64_H__ */
