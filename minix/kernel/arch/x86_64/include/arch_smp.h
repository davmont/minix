#ifndef __SMP_X86_64_H__
#define __SMP_X86_64_H__

#include "arch_proto.h" /* K_STACK_SIZE */

#define MAX_NR_INTERRUPT_ENTRIES	128

#ifndef __ASSEMBLY__

/*
 * Returns the current cpu id.
 *
 * tss_init writes the cpu id as a reg_t (8 bytes) at rsp0 + sizeof(reg_t),
 * which is K_STACK_TOP - 8.  The i386-style `[-1]` of a `u32_t *` reads
 * K_STACK_TOP - 4 — that is the UPPER 4 bytes of the 8-byte cpu id store,
 * which is always 0.  Use `[-2]` to read K_STACK_TOP - 8 = the actual cpu id
 * (its low 32 bits, which is more than enough for CONFIG_MAX_CPUS).
 */
#define cpuid	(((u32_t *)(((u64_t)get_stack_frame() + (K_STACK_SIZE - 1)) \
						& ~(K_STACK_SIZE - 1)))[-2])

/*
 * in case apic or smp is disabled in boot monitor, we need to finish single cpu
 * boot using the legacy PIC
 */
#define smp_single_cpu_fallback() do {		\
	  tss_init(0, get_k_stack_top(0));	\
	  bsp_cpu_id = 0;			\
	  ncpus = 1;				\
	  bsp_finish_booting();			\
} while(0)

extern unsigned char cpuid2apicid[CONFIG_MAX_CPUS];

#ifndef barrier
#define barrier()	do { mfence(); } while(0)
#endif

#endif /* __ASSEMBLY__ */

#endif /* __SMP_X86_64_H__ */
