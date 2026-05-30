/*
 * apic_asm.S - x86_64 APIC interrupt handlers.
 *
 * Adapted from i386 version for 64-bit mode:
 *   - 64-bit registers (%rax, %rdi, etc.)
 *   - IRETQ instead of IRET
 *   - No PUSHA/POPA
 *   - Arguments in registers per SysV ABI
 */

#include "archconst.h"
#include "apic.h"
#include "sconst.h"
#include "apic_asm.h"
#include "include/arch_smp.h"
#include <machine/asm.h>

/*
 * APIC interrupt handler body: pass IRQ number in %rdi (first arg),
 * call irq_handle, then send LAPIC EOI.
 */
#define APIC_IRQ_HANDLER(irq)			\
	movq	$irq, %rdi			;\
	call	_C_LABEL(irq_handle)		;

/*===========================================================================*/
/*			APIC hardware interrupt handlers		     */
/*===========================================================================*/

#define apic_hwint(irq)							\
ENTRY(apic_hwint##irq)							\
	TEST_INT_IN_KERNEL(8, 0f)					;\
									\
	SAVE_PROCESS_CTX(0, KTS_INT_HARD)				;\
	push	%rbp							;\
	mov	%rbp, %rdi	/* context_stop(proc) arg, amd64 ABI */ ;\
	xor	%rbp, %rbp						;\
	call	_C_LABEL(context_stop)					;\
	pop	%rbp							;\
	APIC_IRQ_HANDLER(irq)						;\
	call	_C_LABEL(arch_eoi)					;\
	jmp	_C_LABEL(switch_to_user)				;\
									\
0:									\
	push	%rax							;\
	push	%rcx							;\
	push	%rdx							;\
	push	%rsi							;\
	push	%rdi							;\
	push	%r8							;\
	push	%r9							;\
	push	%r10							;\
	push	%r11							;\
	call	_C_LABEL(context_stop_idle)				;\
	APIC_IRQ_HANDLER(irq)						;\
	call	_C_LABEL(arch_eoi)					;\
	CLEAR_IF(11*8(%rsp))						;\
	pop	%r11							;\
	pop	%r10							;\
	pop	%r9							;\
	pop	%r8							;\
	pop	%rdi							;\
	pop	%rsi							;\
	pop	%rdx							;\
	pop	%rcx							;\
	pop	%rax							;\
	iretq								;

apic_hwint(0)
apic_hwint(1)
apic_hwint(2)
apic_hwint(3)
apic_hwint(4)
apic_hwint(5)
apic_hwint(6)
apic_hwint(7)
apic_hwint(8)
apic_hwint(9)
apic_hwint(10)
apic_hwint(11)
apic_hwint(12)
apic_hwint(13)
apic_hwint(14)
apic_hwint(15)
apic_hwint(16)
apic_hwint(17)
apic_hwint(18)
apic_hwint(19)
apic_hwint(20)
apic_hwint(21)
apic_hwint(22)
apic_hwint(23)
apic_hwint(24)
apic_hwint(25)
apic_hwint(26)
apic_hwint(27)
apic_hwint(28)
apic_hwint(29)
apic_hwint(30)
apic_hwint(31)
apic_hwint(32)
apic_hwint(33)
apic_hwint(34)
apic_hwint(35)
apic_hwint(36)
apic_hwint(37)
apic_hwint(38)
apic_hwint(39)
apic_hwint(40)
apic_hwint(41)
apic_hwint(42)
apic_hwint(43)
apic_hwint(44)
apic_hwint(45)
apic_hwint(46)
apic_hwint(47)
apic_hwint(48)
apic_hwint(49)
apic_hwint(50)
apic_hwint(51)
apic_hwint(52)
apic_hwint(53)
apic_hwint(54)
apic_hwint(55)
apic_hwint(56)
apic_hwint(57)
apic_hwint(58)
apic_hwint(59)
apic_hwint(60)
apic_hwint(61)
apic_hwint(62)
apic_hwint(63)

/*===========================================================================*/
/*			Local APIC interrupt handlers			     */
/*===========================================================================*/

#define LAPIC_INTR_HANDLER(func)					\
	call	_C_LABEL(func)						;\
	call	_C_LABEL(arch_eoi)					;

#define lapic_intr(func)						\
	TEST_INT_IN_KERNEL(8, 0f)					;\
									\
	SAVE_PROCESS_CTX(0, KTS_INT_HARD)				;\
	push	%rbp							;\
	mov	%rbp, %rdi	/* context_stop(proc) arg, amd64 ABI */ ;\
	xor	%rbp, %rbp						;\
	call	_C_LABEL(context_stop)					;\
	pop	%rbp							;\
	LAPIC_INTR_HANDLER(func)					;\
	jmp	_C_LABEL(switch_to_user)				;\
									\
0:									\
	push	%rax							;\
	push	%rcx							;\
	push	%rdx							;\
	push	%rsi							;\
	push	%rdi							;\
	push	%r8							;\
	push	%r9							;\
	push	%r10							;\
	push	%r11							;\
	call	_C_LABEL(context_stop_idle)				;\
	LAPIC_INTR_HANDLER(func)					;\
	CLEAR_IF(11*8(%rsp))						;\
	pop	%r11							;\
	pop	%r10							;\
	pop	%r9							;\
	pop	%r8							;\
	pop	%rdi							;\
	pop	%rsi							;\
	pop	%rdx							;\
	pop	%rcx							;\
	pop	%rax							;\
	iretq								;

ENTRY(lapic_timer_int_handler)
	lapic_intr(timer_int_handler)

ENTRY(apic_spurios_intr)
	iretq

ENTRY(apic_error_intr)
	lapic_intr(apic_error_intr_handler)

#ifdef CONFIG_SMP

ENTRY(apic_ipi_sched_intr)
	/* DBG: '*' = CPU hardware delivered the sched IPI vector.  If this
	 * appears in the serial output but the '<' marker (inside
	 * smp_ipi_sched_handler) does not, then we're stuck in
	 * lapic_intr's context_stop()/BKL_LOCK and the issue is BKL
	 * contention rather than LAPIC delivery. */
	push	%rax
	push	%rdx
	mov	$0x3F8, %dx
	mov	$'*', %al
	out	%al, %dx
	pop	%rdx
	pop	%rax
	lapic_intr(smp_ipi_sched_handler)

ENTRY(apic_ipi_halt_intr)
	lapic_intr(smp_ipi_halt_handler)

#endif /* CONFIG_SMP */

/*===========================================================================*/
/* Dummy lapic interrupt handles for calibration			     */
/*===========================================================================*/
#ifdef APIC_DEBUG

#define LAPIC_INTR_DUMMY_HANDLER_SIZE	32

ENTRY(lapic_intr_dummy_handles_start)

.rept (MAX_NR_INTERRUPT_ENTRIES)
	iretq
	.align LAPIC_INTR_DUMMY_HANDLER_SIZE
.endr

ENTRY(lapic_intr_dummy_handles_end)

#endif /* APIC_DEBUG */
