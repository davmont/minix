/* get_bp.S - return the current base pointer (RBP) */

#include <machine/asm.h>

ENTRY(get_bp)
	movq	%rbp, %rax
	ret
