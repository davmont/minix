/*
 * ipcbench -- measure synchronous IPC round-trip cycles.
 *
 * Each iteration calls getpid(), which in MINIX's libc is an unconditional
 * SENDREC to PM_PROC_NR (see minix/lib/libc/sys/getpid.c) -- no caching.
 * One iteration is therefore one full IPC round-trip through the kernel's
 * do_ipc -> do_sync_ipc -> mini_send / mini_receive path.
 *
 * Reports min / median / p99 / max / mean cycles per round-trip, plus a
 * wall-clock sanity check.  Intended as the Phase-0 baseline for the IPC
 * fastpath project; rerun after each phase to track regressions/wins.
 *
 * Usage: ipcbench [iters]   (default 100000)
 */

#include <sys/types.h>
#include <sys/time.h>
#include <minix/minlib.h>
#include <minix/type.h>
#include <lib.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_ITERS	100000
#define WARMUP_ITERS	1000

static int
cmp_u64(const void *a, const void *b)
{
	u64_t x = *(const u64_t *)a, y = *(const u64_t *)b;
	if (x < y) return -1;
	if (x > y) return 1;
	return 0;
}

int
main(int argc, char **argv)
{
	long iters = DEFAULT_ITERS;
	u64_t *samples;
	u64_t t0, t1, total = 0;
	struct timespec ts0, ts1;
	double wall_sec;
	long i;

	if (argc > 1) {
		iters = strtol(argv[1], NULL, 10);
		if (iters < 100) {
			fprintf(stderr, "ipcbench: need at least 100 iters\n");
			return 1;
		}
	}

	samples = malloc((size_t)iters * sizeof(*samples));
	if (samples == NULL) {
		fprintf(stderr, "ipcbench: malloc(%ld bytes) failed\n",
		    (long)(iters * sizeof(*samples)));
		return 1;
	}

	/* Warmup: fill I-cache, branch predictors, and PM's working set.
	 * Numbers from the first few hundred calls are wildly noisier than
	 * steady state because of TLB and BTB misses. */
	for (i = 0; i < WARMUP_ITERS; i++)
		(void)getpid();

	clock_gettime(CLOCK_MONOTONIC, &ts0);

	for (i = 0; i < iters; i++) {
		read_tsc_64(&t0);
		(void)getpid();
		read_tsc_64(&t1);
		samples[i] = t1 - t0;
		total += samples[i];
	}

	clock_gettime(CLOCK_MONOTONIC, &ts1);

	wall_sec = (double)(ts1.tv_sec - ts0.tv_sec) +
	    (double)(ts1.tv_nsec - ts0.tv_nsec) / 1e9;

	qsort(samples, iters, sizeof(*samples), cmp_u64);

	printf("ipcbench: %ld iterations of getpid() SENDREC round-trip\n",
	    iters);
	printf("  min    : %llu cycles\n", (unsigned long long)samples[0]);
	printf("  p50    : %llu cycles\n",
	    (unsigned long long)samples[iters / 2]);
	printf("  p99    : %llu cycles\n",
	    (unsigned long long)samples[(iters * 99) / 100]);
	printf("  max    : %llu cycles\n",
	    (unsigned long long)samples[iters - 1]);
	printf("  mean   : %llu cycles\n",
	    (unsigned long long)(total / (u64_t)iters));
	printf("  wall   : %.3f s   (%.2f us/op)\n",
	    wall_sec, (wall_sec * 1e6) / (double)iters);

	/* Kernel-side timing instrumentation, if compiled in.  The kuserinfo
	 * page is updated by the kernel on every do_ipc; we snapshot at start
	 * and end and report deltas, separated by fastpath-xcpu / fastpath-
	 * same-cpu / slow-path-SENDREC. */
	{
		struct minix_kerninfo *ki;
		struct kuserinfo *kui_before, *kui_after_buf;
		static struct kuserinfo kui_before_buf;

		ki = get_minix_kerninfo();
		if (ki != NULL && (ki->ki_flags & MINIX_KIF_USERINFO) &&
		    KUSERINFO_HAS_FIELD(ki->kuserinfo, kui_ipcf_xcpu_count)) {
			/* note: instrumentation snapshot is END-only; we
			 * didn't snapshot at start, so totals include boot. */
			struct kuserinfo *kui = ki->kuserinfo;
			(void)kui_before;
			(void)kui_after_buf;
			(void)kui_before_buf;
			printf("\nkernel-side cycle accounting (cumulative since boot):\n");
			printf("  xcpu     count=%-10llu cycles=%-12llu  avg=%llu\n",
			    (unsigned long long)kui->kui_ipcf_xcpu_count,
			    (unsigned long long)kui->kui_ipcf_xcpu_cycles,
			    kui->kui_ipcf_xcpu_count ?
			      (unsigned long long)(kui->kui_ipcf_xcpu_cycles
				/ kui->kui_ipcf_xcpu_count) : 0ULL);
			printf("  same-cpu count=%-10llu cycles=%-12llu  avg=%llu\n",
			    (unsigned long long)kui->kui_ipcf_same_cpu_count,
			    (unsigned long long)kui->kui_ipcf_same_cpu_cycles,
			    kui->kui_ipcf_same_cpu_count ?
			      (unsigned long long)(kui->kui_ipcf_same_cpu_cycles
				/ kui->kui_ipcf_same_cpu_count) : 0ULL);
			printf("  slow-sr  count=%-10llu cycles=%-12llu  avg=%llu\n",
			    (unsigned long long)kui->kui_ipcf_slow_count,
			    (unsigned long long)kui->kui_ipcf_slow_cycles,
			    kui->kui_ipcf_slow_count ?
			      (unsigned long long)(kui->kui_ipcf_slow_cycles
				/ kui->kui_ipcf_slow_count) : 0ULL);
		} else {
			printf("\n(kuserinfo IPC instrumentation not available)\n");
		}
	}

	free(samples);
	return 0;
}
