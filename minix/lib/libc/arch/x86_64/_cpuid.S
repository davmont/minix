/* _cpuid() - interface to cpuid instruction (amd64 SysV ABI) */

/* void _cpuid(u32_t *eax, u32_t *ebx, u32_t *ecx, u32_t *edx);
 *
 * Args: rdi=ptr_eax, rsi=ptr_ebx, rdx=ptr_ecx, rcx=ptr_edx
 */

#include <machine/asm.h>

ENTRY(_cpuid)
	push	%rbx		/* RBX is callee-saved */

	/* Stash the 4 pointer arguments in caller-saved r8-r11 */
	movq	%rdi, %r8	/* r8  = ptr_eax */
	movq	%rsi, %r9	/* r9  = ptr_ebx */
	movq	%rdx, %r10	/* r10 = ptr_ecx */
	movq	%rcx, %r11	/* r11 = ptr_edx */

	/* Load cpuid inputs from the pointed-to values */
	movl	(%r8),  %eax
	movl	(%r9),  %ebx
	movl	(%r10), %ecx
	movl	(%r11), %edx

	cpuid

	/* Store cpuid outputs back through the pointers */
	movl	%eax, (%r8)
	movl	%ebx, (%r9)
	movl	%ecx, (%r10)
	movl	%edx, (%r11)

	pop	%rbx
	ret
