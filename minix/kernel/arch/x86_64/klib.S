/*
 * klib.S - amd64 kernel library routines.
 *
 * Rewritten from arch/i386/klib.S for the AMD64 / SysV calling convention:
 *
 *   Arguments : RDI, RSI, RDX, RCX, R8, R9  (integer/pointer)
 *   Return    : RAX (or RDX:RAX for 128-bit)
 *   Caller-saved scratch: RAX, RCX, RDX, RSI, RDI, R8-R11
 *   Callee-saved       : RBX, RBP, R12-R15
 *
 * Simple leaf functions need no frame; they just execute and RET.
 */

#include <minix/config.h>
#include <minix/const.h>
#include <machine/asm.h>
#include <machine/interrupt.h>
#include <machine/vm.h>
#include "include/archconst.h"
#include "kernel/const.h"
#include "sconst.h"

/*===========================================================================*
 *  Control-register accessors                                               *
 *===========================================================================*/

ENTRY(read_cr0)
	mov	%cr0, %rax
	ret

ENTRY(read_cr2)
	mov	%cr2, %rax
	ret

ENTRY(read_cr3)
	mov	%cr3, %rax
	ret

ENTRY(read_cr4)
	mov	%cr4, %rax
	ret

/* write_cr0(u64 val) */
ENTRY(write_cr0)
	mov	%rdi, %cr0
	jmp	0f      /* serialising jump required after CR0 write */
0:	ret

/* write_cr3(u64 val) */
ENTRY(write_cr3)
	mov	%rdi, %cr3
	ret

/* write_cr4(u64 val) */
ENTRY(write_cr4)
	mov	%rdi, %cr4
	ret

ENTRY(reload_cr3)
	mov	%cr3, %rax
	mov	%rax, %cr3
	ret

/* amd64_invlpg(vir_bytes addr) */
ENTRY(amd64_invlpg)
	invlpg	(%rdi)
	ret

/*===========================================================================*
 *  Descriptor table operations                                              *
 *===========================================================================*/

/* x86_lgdt(void *gdtr)  — pointer to { u16 limit; u64 base } */
ENTRY(x86_lgdt)
	lgdt	(%rdi)
	ret

/* x86_lidt(void *idtr) */
ENTRY(x86_lidt)
	lidt	(%rdi)
	ret

/* x86_ltr(u32 sel) */
ENTRY(x86_ltr)
	ltr	%di
	ret

/* x86_load_kerncs — far-return trick to reload CS with KERN_CS_SELECTOR */
ENTRY(x86_load_kerncs)
	/*
	 * Push CS:RIP pair and use LRETQ to reload CS.
	 * Stack layout for LRETQ: [RIP, CS] (both 8 bytes on stack).
	 */
	movabs	$1f, %rax
	pushq	$KERN_CS_SELECTOR
	pushq	%rax
	lretq
1:	ret

/* Segment register loaders — on amd64 DS/ES/FS/GS/SS are 16-bit selectors */
ENTRY(x86_load_ds)
	mov	%di, %ds
	ret

ENTRY(x86_load_es)
	mov	%di, %es
	ret

ENTRY(x86_load_fs)
	mov	%di, %fs
	ret

ENTRY(x86_load_gs)
	mov	%di, %gs
	ret

ENTRY(x86_load_ss)
	mov	%di, %ss
	ret

/*===========================================================================*
 *  MSR read/write                                                           *
 *===========================================================================*/

/*
 * void ia32_msr_read(u32_t reg, u32_t *hi, u32_t *lo)
 *                    RDI         RSI          RDX
 */
ENTRY(ia32_msr_read)
	mov	%edi, %ecx      /* MSR number */
	rdmsr                   /* result: EDX=hi, EAX=lo */
	mov	%edx, (%rsi)    /* store hi */
	mov	%eax, (%rdx)    /* store lo */
	ret

/*
 * void ia32_msr_write(u32_t reg, u32_t hi, u32_t lo)
 *                     RDI         RSI        RDX
 *
 * WRMSR expects: ECX = MSR#, EDX = hi, EAX = lo.
 * Save lo (RDX) into EAX before RSI overwrites EDX.
 */
ENTRY(ia32_msr_write)
	mov	%edi, %ecx          /* MSR # */
	mov	%edx, %eax          /* lo (3rd arg, RDX) → EAX first */
	mov	%esi, %edx          /* hi (2nd arg, RSI) → EDX */
	wrmsr
	ret

/*===========================================================================*
 *  CPU flags                                                                *
 *===========================================================================*/

ENTRY(read_cpu_flags)
	pushfq
	pop	%rax
	ret

ENTRY(interrupts_enable)
	sti
	ret

ENTRY(interrupts_disable)
	cli
	ret

/*===========================================================================*
 *  FPU helpers                                                              *
 *===========================================================================*/

ENTRY(fninit)
	fninit
	ret

ENTRY(clts)
	clts
	ret

ENTRY(fnstsw)
	xor	%eax, %eax
	fnstsw	%ax
	ret

/* fnstcw(u16_t *cw) — RDI = pointer */
ENTRY(fnstcw)
	fnstcw	(%rdi)
	ret

/* fxsave(void *buf) */
ENTRY(fxsave)
	fxsave	(%rdi)
	ret

/* fnsave(void *buf) */
ENTRY(fnsave)
	fnsave	(%rdi)
	fwait
	ret

/* int fxrstor(void *buf) — returns 0 on success, 1 on fault */
ENTRY(fxrstor)
	fxrstor	(%rdi)
ENTRY(__fxrstor_end)
	xor	%eax, %eax
	ret

/* int frstor(void *buf) */
ENTRY(frstor)
	frstor	(%rdi)
ENTRY(__frstor_end)
	xor	%eax, %eax
	ret

ENTRY(__frstor_failure)
	mov	$1, %eax
	ret

/*===========================================================================*
 *  Misc                                                                     *
 *===========================================================================*/

ENTRY(__main)
	ret

ENTRY(halt_cpu)
	sti
	hlt
	ret

ENTRY(arch_pause)
	pause
	ret

ENTRY(mfence)
	mfence
	ret

ENTRY(read_ebp)
	mov	%rbp, %rax
	ret

ENTRY(x86_triplefault)
	lidt	idt_zero(%rip)
	int	$3
.data
idt_zero:
	.quad	0
	.quad	0
.text

ENTRY(eoi_8259_master)
	movb	$END_OF_INT, %al
	outb	$INT_CTL
	ret

ENTRY(eoi_8259_slave)
	movb	$END_OF_INT, %al
	outb	$INT_CTL
	outb	$INT2_CTL
	ret

ENTRY(refresh_tlb)
	mov	%cr3, %rax
	mov	%rax, %cr3
	ret

/*===========================================================================*
 *  phys_copy(phys_bytes src, phys_bytes dst, phys_bytes n)                 *
 *  RDI=src, RSI=dst, RDX=n                                                 *
 *===========================================================================*/
ENTRY(phys_copy)
	cld
	mov	%rdx, %rcx
	shr	$3, %rcx            /* qword count */
	rep movsq
	mov	%rdx, %rcx
	and	$7, %rcx            /* remaining bytes */
	rep movsb
	xor	%eax, %eax          /* 0 = no fault */
LABEL(phys_copy_fault)
	ret

LABEL(phys_copy_fault_in_kernel)
	mov	%cr2, %rax
	ret

/*===========================================================================*
 *  phys_memset(phys_bytes dst, u64 pat, phys_bytes n)                      *
 *  RDI=dst, RSI=pattern, RDX=n                                             *
 *===========================================================================*/
ENTRY(phys_memset)
	cld
	mov	%rsi, %rax          /* pattern */
	mov	%rdx, %rcx
	shr	$3, %rcx
	rep stosq
	mov	%rdx, %rcx
	and	$7, %rcx
	rep stosb
	xor	%eax, %eax
LABEL(memset_fault)
	ret

LABEL(memset_fault_in_kernel)
	mov	%cr2, %rax
	ret

/*===========================================================================*
 *  copy_msg_from_user(message *user_src, message *dst)                     *
 *  RDI = user source, RSI = kernel destination                             *
 *                                                                           *
 *  A Minix message is 64 bytes (MESSAGE_SIZE).  Copy it as 8 qwords.       *
 *  The exception handler redirects to __user_copy_msg_pointer_failure      *
 *  if a page fault or GP fires during the copy.                             *
 *===========================================================================*/
ENTRY(copy_msg_from_user)
	mov	  0(%rdi), %rax ;  mov %rax,   0(%rsi)
	mov	  8(%rdi), %rax ;  mov %rax,   8(%rsi)
	mov	 16(%rdi), %rax ;  mov %rax,  16(%rsi)
	mov	 24(%rdi), %rax ;  mov %rax,  24(%rsi)
	mov	 32(%rdi), %rax ;  mov %rax,  32(%rsi)
	mov	 40(%rdi), %rax ;  mov %rax,  40(%rsi)
	mov	 48(%rdi), %rax ;  mov %rax,  48(%rsi)
	mov	 56(%rdi), %rax ;  mov %rax,  56(%rsi)
LABEL(__copy_msg_from_user_end)
	xor	%eax, %eax
	ret

/*===========================================================================*
 *  copy_msg_to_user(message *src, message *user_dst)                       *
 *  RDI = kernel source, RSI = user destination                             *
 *===========================================================================*/
ENTRY(copy_msg_to_user)
	mov	  0(%rdi), %rax ;  mov %rax,   0(%rsi)
	mov	  8(%rdi), %rax ;  mov %rax,   8(%rsi)
	mov	 16(%rdi), %rax ;  mov %rax,  16(%rsi)
	mov	 24(%rdi), %rax ;  mov %rax,  24(%rsi)
	mov	 32(%rdi), %rax ;  mov %rax,  32(%rsi)
	mov	 40(%rdi), %rax ;  mov %rax,  40(%rsi)
	mov	 48(%rdi), %rax ;  mov %rax,  48(%rsi)
	mov	 56(%rdi), %rax ;  mov %rax,  56(%rsi)
LABEL(__copy_msg_to_user_end)
	xor	%eax, %eax
	ret

ENTRY(__user_copy_msg_pointer_failure)
	mov	$-1, %rax
	ret

/*===========================================================================*
 *  __switch_address_space(struct proc *p, struct proc **ptproc)            *
 *  RDI = proc ptr, RSI = pointer-to-ptproc                                 *
 *===========================================================================*/
ENTRY(__switch_address_space)
	mov	P_CR3(%rdi), %rax       /* load new CR3 value */
	test	%rax, %rax
	jz	0f
	mov	%cr3, %rcx
	cmp	%rcx, %rax
	je	0f
	mov	%rax, %cr3              /* switch address space */
	mov	%rdi, (%rsi)            /* *ptproc = p */
0:	ret

/*===========================================================================*
 *  switch_k_stack(void *rsp, void (*cont)(void))                           *
 *  RDI = new stack pointer, RSI = continuation function                    *
 *===========================================================================*/
ENTRY(switch_k_stack)
	xor	%rbp, %rbp          /* reset frame pointer for stack traces */
	mov	%rdi, %rsp          /* switch to the new stack */
	jmp	*%rsi               /* jump to the continuation */
	/* NOT_REACHABLE */
0:	jmp	0b

/*===========================================================================*
 *  SMP spinlock primitives                                                  *
 *===========================================================================*/
#ifdef CONFIG_SMP

/* void arch_spinlock_lock(u32_t *lock_data) — RDI = lock */
ENTRY(arch_spinlock_lock)
	mov	$1, %edx            /* back-off initial value */
2:
	mov	$1, %ecx
	xchg	%ecx, (%rdi)
	test	%ecx, %ecx
	je	0f                  /* acquired */

	cmp	$(1 << 16), %edx
	je	1f
	shl	%edx
1:
	mov	%edx, %ecx
3:
	pause
	sub	$1, %ecx
	test	%ecx, %ecx
	jz	2b
	jmp	3b
0:
	mfence
	ret

/* void arch_spinlock_unlock(u32_t *lock_data) — RDI = lock */
ENTRY(arch_spinlock_unlock)
	movl	$0, (%rdi)
	mfence
	ret

#endif /* CONFIG_SMP */

/*===========================================================================*
 *  Port string I/O (used by do_sdevio.c)                                    *
 *  ABI: RDI=port, RSI=buf, RDX=count                                        *
 *===========================================================================*/

/*
 * port I/O string routines — SysV AMD64 ABI:
 *   arg1=RDI(port), arg2=RSI(buf), arg3=RDX(count)
 * insb/insw need: DX=port, RDI=dst, RCX=count
 * outsb/outsw need: DX=port, RSI=src, RCX=count
 */

/* void phys_insw(u16_t port, phys_bytes buf, size_t count) */
ENTRY(phys_insw)
	cld
	mov	%rdi, %r8	/* save port */
	mov	%rsi, %rdi	/* RDI = destination buffer */
	mov	%rdx, %rcx	/* RCX = byte count */
	shr	$1, %rcx	/* word count */
	mov	%r8,  %rdx	/* DX = port */
	rep insw
	ret

/* void phys_insb(u16_t port, phys_bytes buf, size_t count) */
ENTRY(phys_insb)
	cld
	mov	%rdi, %r8	/* save port */
	mov	%rsi, %rdi	/* RDI = destination buffer */
	mov	%rdx, %rcx	/* RCX = byte count */
	mov	%r8,  %rdx	/* DX = port */
	rep insb
	ret

/* void phys_outsw(u16_t port, phys_bytes buf, size_t count) */
ENTRY(phys_outsw)
	cld
	mov	%rdx, %rcx	/* RCX = byte count (save before overwriting RDX) */
	shr	$1, %rcx	/* word count */
	mov	%rdi, %rdx	/* DX = port */
	/* RSI already holds source buffer */
	rep outsw
	ret

/* void phys_outsb(u16_t port, phys_bytes buf, size_t count) */
ENTRY(phys_outsb)
	cld
	mov	%rdx, %rcx	/* RCX = byte count */
	mov	%rdi, %rdx	/* DX = port */
	/* RSI already holds source buffer */
	rep outsb
	ret

/* void poweroff_vmware_clihlt(void) — for VMware/QEMU power-off fallback */
ENTRY(poweroff_vmware_clihlt)
	cli
	hlt
	jmp	poweroff_vmware_clihlt
