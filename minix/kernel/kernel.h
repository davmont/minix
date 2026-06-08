#ifndef KERNEL_H
#define KERNEL_H

/* boot verbose */
#define CONFIG_BOOT_VERBOSE

/*
 * SMP verbose: emit single-character COM1 markers on every cross-CPU
 * sched IPI, every cross-cpu enqueue, every AP iretq to user, and at
 * various AP-bringup stages.  Off by default — the markers were
 * invaluable while debugging the amd64 SMP bringup and we keep them
 * compiled out for production because at ~10 KB/s of marker traffic
 * they saturate the 115200-baud serial line and slow real workloads
 * to a crawl.  Re-enable to diagnose future SMP regressions.
 */
/* #define CONFIG_SMP_VERBOSE */

/*
 * IPC fastpath toggles.
 *
 *   CONFIG_IPC_FASTPATH         (on) — Phase 1 + 4a rendezvous-SENDREC
 *     specialisation in proc.c.  Same-CPU does direct switch via
 *     proc_ptr+switch_address_space; cross-CPU does the existing enqueue
 *     +IPI.  Slow path remains the reference implementation; predicate
 *     misses fall through unchanged.  Comment out to A/B against slow
 *     path only.
 *
 *   CONFIG_IPC_FASTPATH_STATS   (off) — per-branch eligibility-miss
 *     counters dumped on demand.  Diagnostic only.
 *
 *   CONFIG_IPC_FASTPATH_PROBE   (off) — busy-wait probe inserted into
 *     the fastpath hit path to verify (via p50 delta) that it fires.
 *     Diagnostic only.
 *
 *   CONFIG_IPC_FASTPATH_TIMING  (on) — RDTSC cycle accumulators per
 *     call-type (xcpu / same-cpu / slow-sr) exposed via kuserinfo for
 *     userspace analysis (ipcbench reads + prints).  ~5 cycles overhead
 *     per IPC; useful enough as a regression detector to leave on by
 *     default.
 */
#define CONFIG_IPC_FASTPATH
/* #define CONFIG_IPC_FASTPATH_STATS */
/* #define CONFIG_IPC_FASTPATH_PROBE */
#define CONFIG_IPC_FASTPATH_TIMING

#ifndef CONFIG_MAX_CPUS
#define CONFIG_MAX_CPUS	1
#endif

/* OXPCIe952 PCIe with 2 UARTs in-kernel support */
#define CONFIG_OXPCIE	0

/* This is the master header for the kernel.  It includes some other files
 * and defines the principal constants.
 */
#define _SYSTEM            1	/* tell headers that this is the kernel */

/*
 * we need the defines above in assembly files to configure the kernel
 * correctly. However we don't need the rest
 */
#ifndef __ASSEMBLY__

/* The following are so basic, all the *.c files get them automatically. */
#include <minix/config.h>	/* global configuration, MUST be first */
#include <sys/types.h>		/* general system types */
#include <minix/const.h>	/* MINIX specific constants */
#include <minix/type.h>		/* MINIX specific types, e.g. message */
#include <minix/ipc.h>		/* MINIX run-time system */
#include <minix/sysutil.h>	/* MINIX utility library functions */
#include <minix/timers.h>		/* watchdog timer management */
#include <errno.h>		/* return codes and error numbers */
#include <sys/param.h>
#include <minix/param.h>

/* Important kernel header files. */
#include "kernel/config.h"		/* configuration, MUST be first */
#include "kernel/const.h"		/* constants, MUST be second */
#include "kernel/type.h"		/* type definitions, MUST be third */
#include "kernel/proto.h"		/* function prototypes */
#include "kernel/glo.h"		/* global variables */
#include "kernel/ipc.h"		/* IPC constants */
#include "kernel/profile.h"		/* system profiling */
#include "kernel/proc.h"		/* process table */
#include "kernel/cpulocals.h"		/* CPU-local variables */
#include "kernel/debug.h"		/* debugging, MUST be last kernel header */

#ifndef CONFIG_SMP
/* We only support 1 cpu now */
#define CONFIG_MAX_CPUS	1
#define cpuid		0
/* this is always true on an uniprocessor */
#define cpu_is_bsp(x) 1

#else

#include "kernel/smp.h"

#endif

#endif /* __ASSEMBLY__ */

#endif /* KERNEL_H */
