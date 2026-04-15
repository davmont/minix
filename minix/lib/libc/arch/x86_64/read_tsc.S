/*
 * read_tsc.S — x86-64 read_tsc and read_host_time_ns implementations.
 *
 * void read_tsc(u32_t *high, u32_t *low)
 *   Reads the TSC via RDTSC; EDX:EAX -> *high:*low.
 *   rdi = high, rsi = low  (System V AMD64 ABI)
 */
#include <machine/asm.h>

ENTRY(read_tsc)
	rdtsc				/* EDX:EAX = TSC */
	movl	%edx, (%rdi)		/* *high = EDX */
	movl	%eax, (%rsi)		/* *low  = EAX */
	ret

ENTRY(read_host_time_ns)
	movl	$0x10001, %ecx
	rdtsc
	movl	%edx, (%rdi)
	movl	%eax, (%rsi)
	ret
