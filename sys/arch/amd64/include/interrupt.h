/* Interrupt numbers and hardware vectors — x86-64 */

#ifndef _INTERRUPT_H
#define _INTERRUPT_H

/* 8259A interrupt controller ports (same as i386). */
#define INT_CTL         0x20
#define INT_CTLMASK     0x21
#define INT2_CTL        0xA0
#define INT2_CTLMASK    0xA1

/* Magic numbers for interrupt controller. */
#define END_OF_INT      0x20

#define IRQ0_VECTOR     0x50
#define IRQ8_VECTOR     0x70

/* Interrupt vectors defined/reserved by processor. */
#define DIVIDE_VECTOR      0
#define DEBUG_VECTOR       1
#define NMI_VECTOR         2
#define BREAKPOINT_VECTOR  3
#define OVERFLOW_VECTOR    4

/* System-call vectors (via SYSCALL instruction on x86-64). */
#define KERN_CALL_VECTOR_ORIG  32
#define IPC_VECTOR_ORIG        33
#define KERN_CALL_VECTOR_UM    34
#define IPC_VECTOR_UM          35

/* Hardware interrupt numbers. */
#ifndef USE_APIC
#define NR_IRQ_VECTORS    16
#else
#define NR_IRQ_VECTORS    64
#endif
#define CLOCK_IRQ          0
#define KEYBOARD_IRQ       1
#define CASCADE_IRQ        2
#define ETHER_IRQ          3
#define SECONDARY_IRQ      3
#define RS232_IRQ          4
#define XT_WINI_IRQ        5
#define FLOPPY_IRQ         6
#define PRINTER_IRQ        7
#define SPURIOUS_IRQ       7
#define CMOS_CLOCK_IRQ     8
#define KBD_AUX_IRQ       12
#define AT_WINI_0_IRQ     14
#define AT_WINI_1_IRQ     15

#define VECTOR(irq) \
	(((irq) < 8 ? IRQ0_VECTOR : IRQ8_VECTOR) + ((irq) & 0x07))

#endif /* _INTERRUPT_H */
