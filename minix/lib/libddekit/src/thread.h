#ifndef DDEKIT_SRC_THREAD_H
#define DDEKIT_SRC_THREAD_H 1
#include <ddekit/thread.h> 
#include <ddekit/semaphore.h> 
#include <ucontext.h>

#define DDEKIT_THREAD_NAMELEN 32

/*
 * Priority hierarchy (higher value = scheduled first; see
 * _ddekit_thread_schedule which scans ready_queue[] top-down):
 *
 *   0  DISPATCHER  - the idle thread that performs the blocking RECEIVE;
 *                    runs only when nothing else is runnable (dde.c).
 *   1  STDPRIO     - default for ddekit threads (timer, msg-queue waiters).
 *   2  (workers)   - HCD device/worker threads raise themselves here so
 *                    they preempt the default/timer work (hcd.c).
 *   3  IRQPRIO     - interrupt-handler threads (irq.c).  Must sit ABOVE the
 *                    worker threads: an interrupt handler delivers the
 *                    completions those workers are blocked on, so it has to
 *                    preempt them, exactly as ISR threads do on QNX/L4.
 *                    With the handler below the workers, a second runnable
 *                    worker starves it and its device's ISR never fires.
 */
#define DDEKIT_THREAD_PRIOS 4
#define DDEKIT_THREAD_STDPRIO 1
#define DDEKIT_THREAD_IRQPRIO (DDEKIT_THREAD_PRIOS - 1)

#define DDEKIT_THREAD_STACKSIZE (4096*16)

/* This threadlib makes following assumptions:
 *  No Preemption,
 *  No signals,
 *  No blocking syscalls
 *  Threads cooperate.
 */

struct ddekit_thread {
	int id;
	int prio;
	void (*fun)(void *);
	char *stack;
	void *arg;
	void *data;
	unsigned sleep_until;
	char name[DDEKIT_THREAD_NAMELEN];
	ucontext_t ctx;
	ddekit_sem_t *sleep_sem;
	struct ddekit_thread * next;
};


void _ddekit_thread_set_myprio(int prio);
void _ddekit_thread_enqueue(ddekit_thread_t *th);
void _ddekit_thread_dequeue(ddekit_thread_t *th);
void _ddekit_thread_schedule();
void _ddekit_thread_wakeup_sleeping();
void _ddekit_print_backtrace(ddekit_thread_t *th);


#endif
