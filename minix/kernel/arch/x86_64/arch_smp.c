/*
 * arch_smp.c - x86_64 SMP support.
 *
 * APs are brought up via INIT-SIPI-SIPI to a 16-bit real-mode trampoline
 * (see trampoline.S) which transitions through PAE+protected mode into long
 * mode, then jumps into smp_ap_boot() running on a per-CPU kernel stack.
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

	BOOT_VERBOSE(printf(
	    "discover_cpus: entry, ncpus=%u CONFIG_MAX_CPUS=%u\n",
	    ncpus, CONFIG_MAX_CPUS));
	while (ncpus < CONFIG_MAX_CPUS && (cpu = acpi_get_lapic_next())) {
		BOOT_VERBOSE(printf(
		    "discover_cpus: got cpu=%p apic_id=%u flags=0x%x\n",
		    cpu, (unsigned)cpu->apic_id, (unsigned)cpu->flags));
		apicid2cpuid[cpu->apic_id] = ncpus;
		cpuid2apicid[ncpus] = cpu->apic_id;
		printf("CPU %3d local APIC id %3d\n", ncpus, cpu->apic_id);
		ncpus++;
	}
	BOOT_VERBOSE(printf("discover_cpus: exit, ncpus=%u\n", ncpus));

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

/*
 * AP-bringup support (smp_start_aps / ap_finish_booting / smp_ap_boot)
 * is scaffolded but not yet enabled.  See the TODO above the #if 0 in
 * smp_init() for outstanding work on trampoline.S.
 *
 * The block below is the first-draft implementation kept here so we can
 * iterate on it in place once the trampoline is verified.  Until then it
 * is disabled so -Werror doesn't complain about unused statics and so the
 * BSP doesn't disturb its APIC state issuing IPIs to APs that immediately
 * triple-fault.
 */
#if 0
static int volatile ap_cpu_ready;
static phys_bytes trampoline_base;

static u32_t ap_lin_addr(void *vaddr)
{
	assert(trampoline_base);
	return (u32_t)((phys_bytes)(uintptr_t)vaddr -
	    (phys_bytes)(uintptr_t)&trampoline + trampoline_base);
}

static void copy_trampoline(void)
{
	unsigned tramp_size, tramp_start = (unsigned)(uintptr_t)&trampoline;

	assert(!(tramp_start % AMD64_PAGE_SIZE));

	tramp_size = (unsigned)((uintptr_t)&__trampoline_end - tramp_start);
	trampoline_base = alloc_lowest(&kinfo, tramp_size);

	assert(trampoline_base + tramp_size < (1U << 20));
	assert(!(trampoline_base & 0xFFF));

	phys_copy((phys_bytes)(uintptr_t)&trampoline, trampoline_base, tramp_size);
}

static void smp_start_aps(void)
{
	unsigned cpu;

	__ap_pml4 = (u32_t)(read_cr3() & ~(reg_t)0xFFF);

	copy_trampoline();

	for (cpu = 0; cpu < ncpus; cpu++) {
		if (cpu == bsp_cpu_id)
			continue;

		ap_cpu_ready = -1;
		__ap_id    = cpu;
		__ap_stack = (u64_t)(uintptr_t)
		    (get_k_stack_top(cpu) - X86_STACK_TOP_RESERVED);
		__ap_entry = (u64_t)(uintptr_t)smp_ap_boot;

		phys_copy((phys_bytes)(uintptr_t)&trampoline,
		    trampoline_base,
		    (unsigned)((uintptr_t)&__trampoline_end -
			       (uintptr_t)&trampoline));

		mfence();

		if (apic_send_init_ipi(cpu, trampoline_base) ||
		    apic_send_startup_ipi(cpu, trampoline_base)) {
			printf("WARNING cannot boot cpu %d\n", cpu);
			continue;
		}

		lapic_set_timer_one_shot(5000000);
		while (lapic_read(LAPIC_TIMER_CCR)) {
			if (ap_cpu_ready == (int)cpu) {
				cpu_set_flag(cpu, CPU_IS_READY);
				break;
			}
		}
		if (ap_cpu_ready != (int)cpu)
			printf("WARNING : CPU %u didn't boot\n", cpu);
	}
}

static void ap_finish_booting(void)
{
	unsigned cpu = cpuid;

	ap_cpu_ready = cpu;

	spinlock_lock(&boot_lock);
	BKL_LOCK();

	BOOT_VERBOSE(printf("CPU %u is up\n", cpu));

	cpu_identify();
	lapic_enable(cpu);
	fpu_init();

	if (app_cpu_init_timer(system_hz))
		panic("FATAL : failed to initialize timer interrupts CPU %u",
		    cpu);

	get_cpulocal_var(proc_ptr) = get_cpulocal_var_ptr(idle_proc);
	get_cpulocal_var(bill_ptr) = get_cpulocal_var_ptr(idle_proc);

	ap_boot_finished(cpu);
	spinlock_unlock(&boot_lock);

	switch_to_user();
	NOT_REACHABLE;
}

void smp_ap_boot(void)
{
	switch_k_stack((char *)get_k_stack_top(__ap_id) -
	    X86_STACK_TOP_RESERVED, ap_finish_booting);
}
#endif /* AP-bringup scaffold (disabled) */

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

	/*
	 * acpi_init() was already called in cstart on amd64.  Calling it again
	 * here is harmful because the original RSDT physical bytes (along with
	 * the MADT) have been clobbered by pg_alloc_page handing the pages out;
	 * a second parse would corrupt the still-valid sdt_trans[] and S5 state
	 * with garbage.  See acpi.c for the MADT snapshot rationale.
	 */

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

	BOOT_VERBOSE(printf("SMP initialized for %u CPUs (BSP only — AP "
	    "trampoline is a first-draft scaffold, not yet enabled)\n", ncpus));

	/*
	 * TODO(SMP-AP): re-enable when the real-mode → long-mode trampoline
	 * is verified to work end-to-end on KVM.  Outstanding items in
	 * trampoline.S / smp_start_aps():
	 *   - patch ap_ljmp_off with the phys address of long_mode_entry
	 *     before each SIPI (it currently stays 0, so APs jump to RIP=0)
	 *   - phys_copy() needs the *physical* address of &trampoline, not
	 *     its kernel-VA (subtract _kern_offset)
	 *   - verify the ljmpl encoding produces the operand-size prefix in
	 *     .code16 (clang/gas behaviour may differ)
	 * Calling smp_start_aps() with these unresolved breaks APs and the
	 * BSP's APIC state, which (observed) destabilises userland IPC and
	 * crashes cron during multiuser startup.
	 */
#if 0
	smp_start_aps();
#endif
	/*
	 * APs are not actually brought up yet (smp_start_aps above is #if 0),
	 * but discover_cpus() has already incremented ncpus to match the ACPI
	 * MADT count.  Leaving ncpus > 1 means main.c will publish
	 * machine.processors_count > 1 to userland, and the sched server will
	 * happily assign user processes (including init) to APs that never
	 * boot.  The kernel then rejects sys_schedule with EBADCPU
	 * ("PM: An error occurred when trying to schedule 11: -217") and init
	 * never gets to run, hanging the boot.
	 *
	 * Force ncpus back to 1 until smp_start_aps actually works.
	 */
	ncpus = 1;

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
