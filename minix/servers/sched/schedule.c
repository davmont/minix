/* This file contains the scheduling policy for SCHED
 *
 * The entry points are:
 *   do_noquantum:        Called on behalf of process' that run out of quantum
 *   do_start_scheduling  Request to start scheduling a proc
 *   do_stop_scheduling   Request to stop scheduling a proc
 *   do_nice		  Request to change the nice level on a proc
 *   init_scheduling      Called from main.c to set up/prepare scheduling
 */
#include "sched.h"
#include "schedproc.h"
#include <assert.h>
#include <minix/com.h>
#include <machine/archtypes.h>

static unsigned balance_timeout;

#define BALANCE_TIMEOUT	5 /* how often to balance queues in seconds */

static int schedule_process(struct schedproc * rmp, unsigned flags);

#define SCHEDULE_CHANGE_PRIO	0x1
#define SCHEDULE_CHANGE_QUANTUM	0x2
#define SCHEDULE_CHANGE_CPU	0x4

#define SCHEDULE_CHANGE_ALL	(	\
		SCHEDULE_CHANGE_PRIO	|	\
		SCHEDULE_CHANGE_QUANTUM	|	\
		SCHEDULE_CHANGE_CPU		\
		)

#define schedule_process_local(p)	\
	schedule_process(p, SCHEDULE_CHANGE_PRIO | SCHEDULE_CHANGE_QUANTUM)
#define schedule_process_migrate(p)	\
	schedule_process(p, SCHEDULE_CHANGE_CPU)

#define CPU_DEAD	-1

#define cpu_is_available(c)	(cpu_proc[c] >= 0)

#define DEFAULT_USER_TIME_SLICE 200

/* processes created by RS are sysytem processes */
#define is_system_proc(p)	((p)->parent == RS_PROC_NR)

static unsigned cpu_proc[CONFIG_MAX_CPUS];

static void pick_cpu(struct schedproc * proc)
{
#ifdef CONFIG_SMP
	unsigned cpu, c;
	unsigned cpu_load = (unsigned) -1;
	
	if (machine.processors_count == 1) {
		proc->cpu = machine.bsp_id;
		return;
	}

	/* schedule sysytem processes only on the boot cpu */
	if (is_system_proc(proc)) {
		proc->cpu = machine.bsp_id;
		return;
	}

	/* if no other cpu available, try BSP */
	cpu = machine.bsp_id;
	for (c = 0; c < machine.processors_count; c++) {
		/* skip dead cpus */
		if (!cpu_is_available(c))
			continue;
		if (c != machine.bsp_id && cpu_load > cpu_proc[c]) {
			cpu_load = cpu_proc[c];
			cpu = c;
		}
	}
	proc->cpu = cpu;
	cpu_proc[cpu]++;
#else
	proc->cpu = 0;
#endif
}

/*===========================================================================*
 *				do_noquantum				     *
 *===========================================================================*/

int do_noquantum(message *m_ptr)
{
	register struct schedproc *rmp;
	int rv, proc_nr_n;

	if (sched_isokendpt(m_ptr->m_source, &proc_nr_n) != OK) {
		printf("SCHED: WARNING: got an invalid endpoint in OOQ msg %u.\n",
		m_ptr->m_source);
		return EBADEPT;
	}

	rmp = &schedproc[proc_nr_n];
	if (rmp->priority < MIN_USER_Q) {
		rmp->priority += 1; /* lower priority */
	}

	if ((rv = schedule_process_local(rmp)) != OK) {
		return rv;
	}
	return OK;
}

/*===========================================================================*
 *				do_stop_scheduling			     *
 *===========================================================================*/
int do_stop_scheduling(message *m_ptr)
{
	register struct schedproc *rmp;
	int proc_nr_n;

	/* check who can send you requests */
	if (!accept_message(m_ptr))
		return EPERM;

	if (sched_isokendpt(m_ptr->m_lsys_sched_scheduling_stop.endpoint,
		    &proc_nr_n) != OK) {
		printf("SCHED: WARNING: got an invalid endpoint in OOQ msg "
		"%d\n", m_ptr->m_lsys_sched_scheduling_stop.endpoint);
		return EBADEPT;
	}

	rmp = &schedproc[proc_nr_n];
#ifdef CONFIG_SMP
	cpu_proc[rmp->cpu]--;
#endif
	rmp->flags = 0; /*&= ~IN_USE;*/

	return OK;
}

/*===========================================================================*
 *				do_start_scheduling			     *
 *===========================================================================*/
int do_start_scheduling(message *m_ptr)
{
	register struct schedproc *rmp;
	int rv, proc_nr_n, parent_nr_n;
	
	/* we can handle two kinds of messages here */
	assert(m_ptr->m_type == SCHEDULING_START || 
		m_ptr->m_type == SCHEDULING_INHERIT);

	/* check who can send you requests */
	if (!accept_message(m_ptr))
		return EPERM;

	/* Resolve endpoint to proc slot. */
	if ((rv = sched_isemtyendpt(m_ptr->m_lsys_sched_scheduling_start.endpoint,
			&proc_nr_n)) != OK) {
		return rv;
	}
	rmp = &schedproc[proc_nr_n];

	/* Populate process slot */
	rmp->endpoint     = m_ptr->m_lsys_sched_scheduling_start.endpoint;
	rmp->parent       = m_ptr->m_lsys_sched_scheduling_start.parent;
	rmp->max_priority = m_ptr->m_lsys_sched_scheduling_start.maxprio;
	if (rmp->max_priority >= NR_SCHED_QUEUES) {
		return EINVAL;
	}

	/* Inherit current priority and time slice from parent. Since there
	 * is currently only one scheduler scheduling the whole system, this
	 * value is local and we assert that the parent endpoint is valid */
	if (rmp->endpoint == rmp->parent) {
		/* We have a special case here for init, which is the first
		   process scheduled, and the parent of itself. */
		rmp->priority   = USER_Q;
		rmp->time_slice = DEFAULT_USER_TIME_SLICE;

		/*
		 * Since kernel never changes the cpu of a process, all are
		 * started on the BSP and the userspace scheduling hasn't
		 * changed that yet either, we can be sure that BSP is the
		 * processor where the processes run now.
		 */
#ifdef CONFIG_SMP
		rmp->cpu = machine.bsp_id;
		/* FIXME set the cpu mask */
#endif
	}
	
	switch (m_ptr->m_type) {

	case SCHEDULING_START:
		/* We have a special case here for system processes, for which
		 * quanum and priority are set explicitly rather than inherited 
		 * from the parent */
		rmp->priority   = rmp->max_priority;
		rmp->time_slice = m_ptr->m_lsys_sched_scheduling_start.quantum;
		break;
		
	case SCHEDULING_INHERIT:
		/* Inherit current priority and time slice from parent. Since there
		 * is currently only one scheduler scheduling the whole system, this
		 * value is local and we assert that the parent endpoint is valid */
		if ((rv = sched_isokendpt(m_ptr->m_lsys_sched_scheduling_start.parent,
				&parent_nr_n)) != OK)
			return rv;

		rmp->priority = schedproc[parent_nr_n].priority;
		rmp->time_slice = schedproc[parent_nr_n].time_slice;
		break;
		
	default: 
		/* not reachable */
		assert(0);
	}

	/* Take over scheduling the process. The kernel reply message populates
	 * the processes current priority and its time slice */
	if ((rv = sys_schedctl(0, rmp->endpoint, 0, 0, 0)) != OK) {
		printf("Sched: Error taking over scheduling for %d, kernel said %d\n",
			rmp->endpoint, rv);
		return rv;
	}
	rmp->flags = IN_USE;

	/* Schedule the process, giving it some quantum */
	pick_cpu(rmp);
	while ((rv = schedule_process(rmp, SCHEDULE_CHANGE_ALL)) == EBADCPU) {
		/* don't try this CPU ever again */
		cpu_proc[rmp->cpu] = CPU_DEAD;
		pick_cpu(rmp);
	}

	if (rv != OK) {
		printf("Sched: Error while scheduling process, kernel replied %d\n",
			rv);
		return rv;
	}

	/* Mark ourselves as the new scheduler.
	 * By default, processes are scheduled by the parents scheduler. In case
	 * this scheduler would want to delegate scheduling to another
	 * scheduler, it could do so and then write the endpoint of that
	 * scheduler into the "scheduler" field.
	 */

	m_ptr->m_sched_lsys_scheduling_start.scheduler = SCHED_PROC_NR;

	return OK;
}

/*===========================================================================*
 *				do_nice					     *
 *===========================================================================*/
int do_nice(message *m_ptr)
{
	struct schedproc *rmp;
	int rv;
	int proc_nr_n;
	unsigned new_q, old_q, old_max_q;

	/* check who can send you requests */
	if (!accept_message(m_ptr))
		return EPERM;

	if (sched_isokendpt(m_ptr->m_pm_sched_scheduling_set_nice.endpoint, &proc_nr_n) != OK) {
		printf("SCHED: WARNING: got an invalid endpoint in OoQ msg "
		"%d\n", m_ptr->m_pm_sched_scheduling_set_nice.endpoint);
		return EBADEPT;
	}

	rmp = &schedproc[proc_nr_n];
	new_q = m_ptr->m_pm_sched_scheduling_set_nice.maxprio;
	if (new_q >= NR_SCHED_QUEUES) {
		return EINVAL;
	}

	/* Store old values, in case we need to roll back the changes */
	old_q     = rmp->priority;
	old_max_q = rmp->max_priority;

	/* Update the proc entry and reschedule the process */
	rmp->max_priority = rmp->priority = new_q;

	if ((rv = schedule_process_local(rmp)) != OK) {
		/* Something went wrong when rescheduling the process, roll
		 * back the changes to proc struct */
		rmp->priority     = old_q;
		rmp->max_priority = old_max_q;
	}

	return rv;
}

/*===========================================================================*
 *				schedule_process			     *
 *===========================================================================*/
static int schedule_process(struct schedproc * rmp, unsigned flags)
{
	int err;
	int new_prio, new_quantum, new_cpu, niced;

	pick_cpu(rmp);

	if (flags & SCHEDULE_CHANGE_PRIO)
		new_prio = rmp->priority;
	else
		new_prio = -1;

	if (flags & SCHEDULE_CHANGE_QUANTUM)
		new_quantum = rmp->time_slice;
	else
		new_quantum = -1;

	if (flags & SCHEDULE_CHANGE_CPU)
		new_cpu = rmp->cpu;
	else
		new_cpu = -1;

	niced = (rmp->max_priority > USER_Q);

	if ((err = sys_schedule(rmp->endpoint, new_prio,
		new_quantum, new_cpu, niced)) != OK) {
		printf("PM: An error occurred when trying to schedule %d: %d\n",
		rmp->endpoint, err);
	}

	return err;
}


/*===========================================================================*
 *				init_scheduling				     *
 *===========================================================================*/
void init_scheduling(void)
{
	int r;

	balance_timeout = BALANCE_TIMEOUT * sys_hz();

	if ((r = sys_setalarm(balance_timeout, 0)) != OK)
		panic("sys_setalarm failed: %d", r);
}

/*===========================================================================*
 *				colocation policy (Tier 1)		     *
 *===========================================================================*
 * Migrate system servers toward the CPU where their IPC traffic is
 * concentrated.  Decision data comes from the kernel's per-proc
 * p_ipc_sender_cpu_count[] histogram (read+cleared via GET_IPCTRAFFIC).
 *
 * Anti-thrash: require dominance to persist N intervals before acting;
 * apply exponential backoff on repeat migrations of the same proc.
 */
#ifdef CONFIG_SMP
#define COLOC_DOMINANCE_PCT		70  /* threshold % traffic from one CPU */
#define COLOC_STABLE_INTERVALS		1   /* intervals of dominance before act
					     * (cooldown + exp backoff still
					     * prevent thrash on transients) */
#define COLOC_BASE_COOLDOWN		4   /* base intervals between migrations */
#define COLOC_MIN_TOTAL_TRAFFIC		100 /* min IPCs in window to act on */
#define COLOC_MIGRATIONS_DECAY_AFTER	8   /* clear migrations_recent after */

static struct colocation_state {
	int last_dominant_cpu;
	int stable_intervals;
	int cooldown_remaining;
	int migrations_recent;	/* for exponential backoff */
	int quiet_intervals;	/* for decaying migrations_recent */
} coloc[NR_PROCS];

static void colocate_system_servers(void)
{
	struct schedproc *rmp;
	u32_t hist[CONFIG_MAX_CPUS];
	int proc_nr, c, r;
	u32_t total, max_count;
	int dominant_cpu, dominance_pct;
	struct colocation_state *cs;
	static int boot_grace_intervals = 2;  /* 10 sec (2 × BALANCE_TIMEOUT=5s) */

	if (machine.processors_count <= 1)
		return;

	/* Don't migrate during boot — server placement assumptions are
	 * load-bearing for boot sequence (PM/VFS/etc. expected on BSP). */
	if (boot_grace_intervals > 0) {
		boot_grace_intervals--;
		return;
	}

	for (proc_nr = 0, rmp = schedproc; proc_nr < NR_PROCS;
	    proc_nr++, rmp++) {
		if (!(rmp->flags & IN_USE)) continue;
		/* Never migrate critical system procs that have CPU-affinity
		 * assumptions baked in (clock delivery, IPC fan-in routing). */
		if (rmp->endpoint == SCHED_PROC_NR) continue;  /* don't move ourselves */
		if (rmp->endpoint == RS_PROC_NR) continue;
		if (rmp->endpoint == VM_PROC_NR) continue;
		if (rmp->endpoint == VFS_PROC_NR) continue;
		if (rmp->endpoint == CLOCK) continue;
		if (rmp->endpoint == SYSTEM) continue;
		/* Don't restrict to is_system_proc() — boot-time servers
		 * (PM/VFS/etc) have parent != RS_PROC_NR so they'd be
		 * excluded.  Use the explicit blacklist above plus the
		 * dominance + stable-intervals + cooldown heuristics. */

		cs = &coloc[proc_nr];

		/* Decay migrations_recent during quiet periods. */
		if (cs->cooldown_remaining > 0) {
			cs->cooldown_remaining--;
			cs->quiet_intervals = 0;
			continue;
		}
		cs->quiet_intervals++;
		if (cs->quiet_intervals >= COLOC_MIGRATIONS_DECAY_AFTER &&
		    cs->migrations_recent > 0) {
			cs->migrations_recent--;
			cs->quiet_intervals = 0;
		}

		/* Pull histogram from kernel.  Read also clears it; next
		 * interval's data is fresh. */
		r = sys_getinfo(GET_IPCTRAFFIC, hist, sizeof(hist),
		    NULL, rmp->endpoint);
		if (r != OK) continue;

		total = 0;
		max_count = 0;
		dominant_cpu = -1;
		for (c = 0; c < machine.processors_count; c++) {
			total += hist[c];
			if (hist[c] > max_count) {
				max_count = hist[c];
				dominant_cpu = c;
			}
		}
		if (total < COLOC_MIN_TOTAL_TRAFFIC) {
			cs->last_dominant_cpu = -1;
			cs->stable_intervals = 0;
			continue;
		}
		dominance_pct = (int)((max_count * 100) / total);
		if (dominance_pct < COLOC_DOMINANCE_PCT) {
			cs->last_dominant_cpu = -1;
			cs->stable_intervals = 0;
			continue;
		}

		/* Persistent dominance? */
		if (dominant_cpu == cs->last_dominant_cpu) {
			cs->stable_intervals++;
		} else {
			cs->last_dominant_cpu = dominant_cpu;
			cs->stable_intervals = 1;
		}

		if (cs->stable_intervals < COLOC_STABLE_INTERVALS) continue;
		if (dominant_cpu == (int)rmp->cpu) continue;  /* already there */
		if (dominant_cpu == CPU_DEAD ||
		    !cpu_is_available(dominant_cpu)) continue;

		/* Migrate.  Apply exponential backoff on repeat migrations.
		 *
		 * IMPORTANT: schedule_process_migrate(rmp) calls schedule_process
		 * which calls pick_cpu(rmp) which OVERWRITES rmp->cpu with its
		 * own load-balancing decision (BSP for system procs, least-loaded
		 * non-BSP for users).  That clobbers our explicit dominant_cpu
		 * choice.  Bypass it by calling sys_schedule directly with our
		 * chosen CPU.
		 *
		 * sched_proc's lazy migration path now releases FPU ownership
		 * and clears MF_FPU_INITIALIZED so the proc gets a fresh xrstor
		 * on its new CPU — fixes the cross-CPU FPU-restore FPE.
		 */
		{
			int niced_bit = (rmp->max_priority > USER_Q);
			int r = sys_schedule(rmp->endpoint,
			    -1 /* prio: no change */,
			    -1 /* quantum: no change */,
			    dominant_cpu,
			    niced_bit);
			if (r == OK) {
				rmp->cpu = dominant_cpu;
				cs->cooldown_remaining = COLOC_BASE_COOLDOWN *
				    (1 + cs->migrations_recent);
				cs->migrations_recent++;
				cs->stable_intervals = 0;
				cs->quiet_intervals = 0;
			}
		}
	}
}
#endif /* CONFIG_SMP */

/*===========================================================================*
 *				balance_queues				     *
 *===========================================================================*/

/* This function in called every N ticks to rebalance the queues. The current
 * scheduler bumps processes down one priority when ever they run out of
 * quantum. This function will find all proccesses that have been bumped down,
 * and pulls them back up. This default policy will soon be changed.
 */
void balance_queues(void)
{
	struct schedproc *rmp;
	int r, proc_nr;

	for (proc_nr=0, rmp=schedproc; proc_nr < NR_PROCS; proc_nr++, rmp++) {
		if (rmp->flags & IN_USE) {
			if (rmp->priority > rmp->max_priority) {
				rmp->priority -= 1; /* increase priority */
				schedule_process_local(rmp);
			}
		}
	}

#ifdef CONFIG_SMP
	colocate_system_servers();
#endif

	if ((r = sys_setalarm(balance_timeout, 0)) != OK)
		panic("sys_setalarm failed: %d", r);
}
