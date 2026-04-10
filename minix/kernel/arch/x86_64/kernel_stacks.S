/*
 * kernel_stacks.S — paged-kernel stack BSS.
 *
 * These symbols live in the paged .bss section at the kernel's high VMA.
 * Paged C code (protect.c, arch_system.c) references them after the MMU
 * page tables are up.
 *
 * The corresponding early-boot stacks in head.S (e.g. k_initial_stack)
 * end up in the unpaged namespace as __k_unpaged_* and are separate.
 * head.S uses a low-physical-address stack via its own unpaged BSS during
 * the bootstrap phase before paging is fully established.
 */

#include <machine/asm.h>
#include <minix/config.h>

/* Must match K_STACK_SIZE = AMD64_PAGE_SIZE defined in include/arch_proto.h */
#define K_STACK_SIZE	4096

.bss

/*
 * Per-CPU kernel stacks.
 * Layout: two K_STACK_SIZE pages per CPU (data page + guard page).
 * arch_system.c sets k_stacks = &k_stacks_start; get_k_stack_top() uses it.
 */
.align K_STACK_SIZE
LABEL(k_stacks_start)
	.space	(2 * K_STACK_SIZE * CONFIG_MAX_CPUS)
LABEL(k_stacks_end)

/*
 * CPU 0 initial kernel stack.
 * protect.c initialises TSS.RSP0 for CPU 0 with &k_boot_stktop so that
 * ring-0 entry (interrupt, exception) uses this stack.
 */
.align K_STACK_SIZE
k_boot_stack_data:
	.space	K_STACK_SIZE
LABEL(k_initial_stktop)
LABEL(k_boot_stktop)
