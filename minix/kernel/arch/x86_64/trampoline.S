/*
 * trampoline.S - x86_64 AP startup trampoline.
 *
 * When the BSP issues an INIT-SIPI-SIPI IPI sequence, each AP starts
 * execution at a 4 KB-aligned physical address below 1 MB in 16-bit real
 * mode with CS:IP = (vector << 8):0000.  This file is a self-contained blob
 * that is copied to such an address by smp_start_aps() and:
 *
 *   - loads a temporary GDT with 64-bit code/data descriptors,
 *   - enables PAE in CR4,
 *   - sets the BSP's PML4 phys address in CR3 (so the kernel mapping is live),
 *   - sets EFER.LME (long mode enable),
 *   - sets CR0.PG | CR0.PE simultaneously to enter long mode,
 *   - far-jumps to 64-bit code at a high kernel virtual address,
 *   - loads data segments, sets the per-AP stack, jumps to smp_ap_boot().
 *
 * Data exchanged with the BSP lives at well-known offsets inside the blob
 * (ap_id, ap_pml4, ap_stack, ap_entry).  The BSP patches these before each
 * SIPI; the trampoline reads them via CS-relative addressing while still in
 * real mode (CS is set to (blob_phys >> 4) by the SIPI).
 *
 * The blob lives in its own section (.text.trampoline) so it can be located
 * via labels and copied verbatim to low physical memory.
 */

#include <machine/asm.h>
#include "archconst.h"

.section .text.trampoline, "ax"
.balign 4096
.globl _C_LABEL(trampoline)
.globl _C_LABEL(__ap_id)
.globl _C_LABEL(__ap_pml4)
.globl _C_LABEL(__ap_stack)
.globl _C_LABEL(__ap_entry)
.globl _C_LABEL(__trampoline_end)

.code16
_C_LABEL(trampoline):
	cli
	cld

	/*
	 * SIPI loaded CS = (blob_phys >> 4), IP = 0.  Make DS = CS so we can
	 * access trampoline-local data with offsets relative to the blob base.
	 */
	mov	%cs, %ax
	mov	%ax, %ds
	mov	%ax, %es
	mov	%ax, %ss
	xor	%sp, %sp

	/* Load the temporary 32/64-bit GDT.  Its base will be patched at
	 * runtime by the BSP to be the absolute physical address of ap_gdt
	 * (computed as blob_phys + (ap_gdt - trampoline)). */
	lgdtl	ap_gdt_descr - _C_LABEL(trampoline)

	/* Enable PAE (required for long mode). */
	mov	%cr4, %eax
	or	$0x20, %eax		/* CR4.PAE */
	mov	%eax, %cr4

	/* Set CR3 = BSP's PML4 physical address (patched by BSP). */
	mov	ap_pml4_lo - _C_LABEL(trampoline), %eax
	mov	%eax, %cr3

	/* Enable Long Mode in EFER (MSR 0xC0000080), bit 8 (LME). */
	mov	$0xC0000080, %ecx
	rdmsr
	or	$0x100, %eax
	wrmsr

	/* Switch directly to long mode by setting CR0.PG | CR0.PE in one
	 * write.  After this we are in compatibility submode of IA-32e; the
	 * far jump below transitions to true 64-bit. */
	mov	%cr0, %eax
	or	$0x80000001, %eax	/* CR0.PG | CR0.PE */
	mov	%eax, %cr0

	/*
	 * The ljmpl operand encoding takes a 48-bit ptr16:32 (or 16:16).  We
	 * use a 16:32 form pointing into the trampoline's own .code64 stub.
	 * The selector is our 64-bit code segment in ap_gdt (index 1).  The
	 * offset is the physical address of long_mode_entry, which the BSP
	 * patches into ap_ljmp_off.
	 */
	ljmpl	*(ap_ljmp_ptr - _C_LABEL(trampoline))

/*
 * 64-bit entry.  We are now in long mode but still on the temporary
 * stack-less environment.  Reload data segments to the kernel data
 * selector, set up the AP's kernel stack, then jump to the C entry.
 *
 * The C entry's address is left in ap_entry (kernel virtual address);
 * once we are in long mode that address is dereferenceable as-is because
 * the BSP's PML4 has the kernel mapping at PML4[511].
 */
.code64
.balign 8
long_mode_entry:
	/* Load data segment selectors to KERN_DS. */
	mov	$KERN_DS_SELECTOR, %ax
	mov	%ax, %ds
	mov	%ax, %es
	mov	%ax, %fs
	mov	%ax, %gs
	mov	%ax, %ss

	/*
	 * Switch to the AP's kernel stack (patched by the BSP to point at
	 * get_k_stack_top(cpuid) - X86_STACK_TOP_RESERVED).  ap_stack is a
	 * 64-bit absolute kernel-virtual address.
	 */
	movabs	$ap_stack_kvirt, %rax
	mov	(%rax), %rsp
	xor	%rbp, %rbp

	/*
	 * Call the C entry point.  ap_entry holds the absolute kernel-virtual
	 * address of smp_ap_boot (set once by the BSP at trampoline-prep time).
	 */
	movabs	$ap_entry_kvirt, %rax
	jmp	*(%rax)

/* ===========================================================================
 * Data area for trampoline.  The BSP patches these before each SIPI.
 *
 * NOTE: The labels below (ap_pml4_lo, ap_ljmp_off, etc.) are addresses
 * INSIDE the trampoline blob, accessed by the trampoline via CS-relative
 * addressing in real mode.  Three additional symbols (ap_id_kvirt /
 * ap_stack_kvirt / ap_entry_kvirt) are absolute kernel-VA pointers used
 * after we are in 64-bit mode, populated by the BSP-side smp_start_aps().
 * ===========================================================================
 */
.balign 8

/* gdtr operand: limit + 32-bit base.  Base is patched to (blob_phys + offset
 * of ap_gdt) before SIPI. */
ap_gdt_descr:
	.word	ap_gdt_end - ap_gdt - 1
	.long	0		/* patched: phys addr of ap_gdt */

/* 32-bit ljmpl operand: 32-bit offset + 16-bit selector.  Offset is patched
 * to the physical address of long_mode_entry; selector is KERN_CS. */
ap_ljmp_ptr:
ap_ljmp_off:
	.long	0		/* patched: phys addr of long_mode_entry */
	.word	KERN_CS_SELECTOR

/* BSP's PML4 physical address (32 bits; PML4 lives in low memory). */
ap_pml4_lo:
	.long	0		/* patched: BSP cr3 */

/* AP id (logical CPU index), readable by C in long mode. */
.globl _C_LABEL(__ap_id)
_C_LABEL(__ap_id):
	.long	0		/* patched: cpu index */

/* Two absolute kernel-VA pointers patched by the BSP.  After we are in
 * long mode the kernel mapping at PML4[511] is live and these addresses
 * are valid. */
ap_stack_kvirt:
	.quad	0		/* patched: kernel-VA of AP's stack top */
ap_entry_kvirt:
	.quad	0		/* patched: kernel-VA of smp_ap_boot */

/* C-visible aliases — easier to patch from arch_smp.c without computing
 * offsets manually.  They overlap the labels above. */
.globl _C_LABEL(__ap_pml4)
_C_LABEL(__ap_pml4) = ap_pml4_lo
.globl _C_LABEL(__ap_stack)
_C_LABEL(__ap_stack) = ap_stack_kvirt
.globl _C_LABEL(__ap_entry)
_C_LABEL(__ap_entry) = ap_entry_kvirt
.globl _C_LABEL(__ap_gdt_descr)
_C_LABEL(__ap_gdt_descr) = ap_gdt_descr
.globl _C_LABEL(__ap_ljmp_off)
_C_LABEL(__ap_ljmp_off) = ap_ljmp_off

/* ===========================================================================
 * The temporary GDT used by the trampoline.
 *
 * Entry layout (matches what archconst.h defines for the main kernel):
 *   index 0: null
 *   index 1: 64-bit code segment, DPL=0 (KERN_CS)
 *   index 2: 64-bit data segment, DPL=0 (KERN_DS)
 *
 * The 64-bit code descriptor has L=1 (long mode), D=0, and the access byte
 * is 0x9A (Present|DPL=0|Code|Readable).
 * The data descriptor uses 0x92 (Present|DPL=0|Data|Writable).
 * ===========================================================================
 */
.balign 16
ap_gdt:
	.quad	0x0000000000000000	/* index 0: null */
	.quad	0x00209A0000000000	/* index 1: 64-bit code, KERN_CS */
	.quad	0x0000920000000000	/* index 2: 64-bit data, KERN_DS */
ap_gdt_end:

/* Mark the end of the blob so arch_smp.c can size the copy. */
.balign 16
_C_LABEL(__trampoline_end):
