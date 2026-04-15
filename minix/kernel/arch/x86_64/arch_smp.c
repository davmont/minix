/*
 * arch_smp.c - x86_64 SMP support.
 *
 * Adapted from arch/i386/arch_smp.c.  The AP trampoline on x86_64 is
 * fundamentally the same as on i386 (APs start in real mode), but setting
 * up 64-bit long mode for each AP is non-trivial and is left as a TODO.
 *
 * For now, smp_init() discovers CPUs via ACPI but falls back to uniprocessor
 * mode, which is sufficient to boot.  All other entry points needed by the
 * generic SMP layer are provided.
 */

#define _SMP

#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <machine/cmos.h>
#include <machine/bios.h>

#include "kernel/spinlock.h"
#include "kernel/smp.h"
#include "apic.h"
#include "acpi.h"
#include "kernel/clock.h"

#include "glo.h"

/* there can be at most 255 local APIC ids, each fits in 8 bits */
static unsigned char apicid2cpuid[255];
unsigned char cpuid2apicid[CONFIG_MAX_CPUS];

SPINLOCK_DEFINE(smp_cpu_lock)
SPINLOCK_DEFINE(dispq_lock)

static int volatile cpu_down;

static void smp_reinit_vars(void)
{
	lapic_addr = lapic_eoi_addr = 0;
	ioapic_enabled = 0;

	ncpus = 1;
}

static void tss_init_all(void)
{
	unsigned cpu;

	for (cpu = 0; cpu < ncpus; cpu++)
		tss_init(cpu, get_k_stack_top(cpu));
}

static int discover_cpus(void)
{
	struct acpi_madt_lapic *cpu;

	while (ncpus < CONFIG_MAX_CPUS && (cpu = acpi_get_lapic_next())) {
		apicid2cpuid[cpu->apic_id] = ncpus;
		cpuid2apicid[ncpus] = cpu->apic_id;
		printf("CPU %3d local APIC id %3d\n", ncpus, cpu->apic_id);
		ncpus++;
	}

	return ncpus;
}

void smp_shutdown_aps(void)
{
	unsigned cpu;

	if (ncpus == 1)
		goto exit_shutdown_aps;

	BKL_UNLOCK();

	for (cpu = 0; cpu < ncpus; cpu++) {
		if (cpu == cpuid)
			continue;
		if (!cpu_test_flag(cpu, CPU_IS_READY)) {
			printf("CPU %d didn't boot\n", cpu);
			continue;
		}

		cpu_down = -1;
		barrier();
		apic_send_ipi(APIC_SMP_CPU_HALT_VECTOR, cpu, APIC_IPI_DEST);
		while (cpu_down != cpu)
			;
		printf("CPU %d is down\n", cpu);
		cpu_clear_flag(cpu, CPU_IS_READY);
	}

exit_shutdown_aps:
	ioapic_disable_all();
	lapic_disable();

	ncpus = 1;
	lapic_addr = lapic_eoi_addr = 0;
}

void smp_init(void)
{
	/* Read the MP configuration. */
	if (!discover_cpus()) {
		ncpus = 1;
		goto uniproc_fallback;
	}

	lapic_addr = LOCAL_APIC_DEF_ADDR;
	ioapic_enabled = 0;

	tss_init_all();

	bsp_cpu_id = apicid2cpuid[apicid()];

	if (!lapic_enable(bsp_cpu_id)) {
		printf("ERROR : failed to initialize BSP Local APIC\n");
		goto uniproc_fallback;
	}

	bsp_lapic_id = apicid();

	acpi_init();

	if (!detect_ioapics()) {
		lapic_disable();
		lapic_addr = 0x0;
		goto uniproc_fallback;
	}

	ioapic_enable_all();

	if (ioapic_enabled)
		machine.apic_enabled = 1;

	/* Set SMP IDT entries. */
	apic_idt_init(0); /* not a reset */
	idt_reload();

	BOOT_VERBOSE(printf("SMP initialized (uniprocessor mode)\n"));

	/*
	 * AP boot on x86_64 requires a 16-bit real-mode trampoline that
	 * transitions each AP through protected mode into long mode.  This is
	 * not yet implemented; fall through to bsp_finish_booting() and run
	 * single-CPU only.
	 */
	bsp_finish_booting();
	NOT_REACHABLE;

uniproc_fallback:
	apic_idt_init(1); /* reset to PIC IDT */
	idt_reload();
	smp_reinit_vars();
	intr_init(0); /* no auto EOI */
	printf("WARNING : SMP initialization failed\n");
}

void arch_smp_halt_cpu(void)
{
	cpu_down = cpuid;
	barrier();
	BKL_UNLOCK();
	for (;;)
		;
}

void arch_send_smp_schedule_ipi(unsigned cpu)
{
	apic_send_ipi(APIC_SMP_SCHED_PROC_VECTOR, cpu, APIC_IPI_DEST);
}
