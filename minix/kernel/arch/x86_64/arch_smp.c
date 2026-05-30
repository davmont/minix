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

/* Raw COM1 print bypassing kernel console state — same trick as main.c's
 * __kmain_mark, used for early SMP debug since printf goes to VGA before the
 * serial console is wired up. */
static inline void __smp_com1(char c) {
	__asm__ __volatile__("outb %0, %1" : : "a"(c),
	    "Nd"((unsigned short)0x3F8));
}
static inline void __smp_mark(const char *s) {
	while (*s) __smp_com1(*s++);
	__smp_com1('\r'); __smp_com1('\n');
}

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
extern char trampoline[], __trampoline_end[];
extern char __long_mode_entry[], __ap_gdt[], __ap_gdt_descr[];
extern u32_t __ap_id, __ap_pml4, __ap_ljmp_off;
extern u64_t __ap_stack, __ap_entry;

static int volatile ap_cpu_ready;
static phys_bytes trampoline_base;

void smp_ap_boot(void);
static void ap_finish_booting(void);

static u32_t blob_phys_of(const void *kva)
{
	assert(trampoline_base);
	return (u32_t)(trampoline_base +
	    ((phys_bytes)(uintptr_t)kva - (phys_bytes)(uintptr_t)&trampoline));
}

static void copy_trampoline(void)
{
	unsigned tramp_size, tramp_start = (unsigned)(uintptr_t)&trampoline;

	assert(!(tramp_start % AMD64_PAGE_SIZE));

	tramp_size = (unsigned)((uintptr_t)&__trampoline_end - tramp_start);
	trampoline_base = alloc_lowest(&kinfo, tramp_size);

	assert(trampoline_base + tramp_size < (1U << 20));
	assert(!(trampoline_base & 0xFFF));

	__ap_ljmp_off = blob_phys_of(__long_mode_entry);
	*(u32_t *)&__ap_gdt_descr[2] = blob_phys_of(__ap_gdt);
	__ap_pml4 = (u32_t)(read_cr3() & ~(reg_t)0xFFF);

	phys_copy((phys_bytes)(uintptr_t)&trampoline, trampoline_base,
	    tramp_size);
}

static void smp_start_aps(void)
{
	unsigned cpu;

	__smp_mark("START_APS-entry");
	__ap_pml4 = (u32_t)(read_cr3() & ~(reg_t)0xFFF);
	__smp_mark("START_APS-post-cr3");

	copy_trampoline();
	__smp_mark("START_APS-post-copy_trampoline");

	for (cpu = 0; cpu < ncpus; cpu++) {
		if (cpu == bsp_cpu_id)
			continue;
		__smp_mark("START_APS-loop");

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
		__smp_mark("START_APS-pre-init_ipi");

		if (apic_send_init_ipi(cpu, trampoline_base) ||
		    apic_send_startup_ipi(cpu, trampoline_base)) {
			__smp_mark("START_APS-ipi-FAIL");
			continue;
		}
		__smp_mark("START_APS-post-startup_ipi");

		/*
		 * Wait up to ~500 ms for the AP to report ready.  Using TSC
		 * directly because the i386-style LAPIC_TIMER_CCR poll doesn't
		 * work on amd64 when TSC-deadline mode is in use (ICR/CCR are
		 * stale from apic_calibrate_clocks and never get rearmed).
		 */
		{
			u64_t tsc_now, tsc_deadline;
			u64_t mhz = (u64_t)cpu_info[bsp_cpu_id].freq;
			if (mhz == 0) mhz = 1000;	/* safe fallback */

			read_tsc_64(&tsc_now);
			tsc_deadline = tsc_now + mhz * 500000ULL;	/* 0.5 s */

			while (ap_cpu_ready != (int)cpu) {
				read_tsc_64(&tsc_now);
				if (tsc_now >= tsc_deadline)
					break;
			}
		}
		if (ap_cpu_ready != (int)cpu)
			__smp_mark("START_APS-CPU-NO-BOOT");
		else {
			cpu_set_flag(cpu, CPU_IS_READY);
			__smp_mark("START_APS-CPU-UP");
		}
	}
	__smp_mark("START_APS-done");
}

static void ap_finish_booting(void)
{
	unsigned cpu;
	__smp_com1('f');

	cpu = cpuid;
	__smp_com1('1');

	/*
	 * Per-CPU setup that the trampoline doesn't do: replace the temporary
	 * trampoline GDT with the kernel GDT, load the IDT, point TR at this
	 * AP's TSS, and program IA32_KERNEL_GS_BASE for the SYSCALL path.
	 * Must happen BEFORE ap_cpu_ready is set so the BSP can't send us an
	 * IPI before we have an IDT.
	 */
	x86_lgdt(&gdt_desc);
	__smp_com1('g');
	idt_reload();
	__smp_com1('i');
	x86_ltr(TSS_SELECTOR(cpu));
	__smp_com1('t');
	ap_set_kernel_gs_base(cpu);
	__smp_com1('s');
	ap_setup_syscall_msrs();
	__smp_com1('y');

	ap_cpu_ready = cpu;
	__smp_com1('2');

	spinlock_lock(&boot_lock);
	__smp_com1('3');
	BKL_LOCK();
	__smp_com1('4');

	BOOT_VERBOSE(printf("CPU %u is up\n", cpu));

	cpu_identify();
	__smp_com1('5');
	cpu_enable_features();	/* NXE, FSGSBASE, PCIDE — must match BSP */
	__smp_com1('e');
	lapic_enable(cpu);
	__smp_com1('6');
	fpu_init();
	__smp_com1('7');

	if (app_cpu_init_timer(system_hz))
		panic("FATAL : failed to initialize timer interrupts CPU %u",
		    cpu);
	__smp_com1('8');

	get_cpulocal_var(proc_ptr) = get_cpulocal_var_ptr(idle_proc);
	get_cpulocal_var(bill_ptr) = get_cpulocal_var_ptr(idle_proc);
	__smp_com1('9');

	ap_boot_finished(cpu);
	spinlock_unlock(&boot_lock);
	__smp_com1('!');

	switch_to_user();
	NOT_REACHABLE;
}

void smp_ap_boot(void)
{
	__smp_com1('b');
	switch_k_stack((char *)get_k_stack_top(__ap_id) -
	    X86_STACK_TOP_RESERVED, ap_finish_booting);
}
/* end of un-#if-0'd scaffold */

void smp_init(void)
{
	__smp_mark("SMP_INIT-entry");
	/* Read the MP configuration. */
	if (!discover_cpus()) {
		__smp_mark("SMP_INIT-no_cpus");
		ncpus = 1;
		goto uniproc_fallback;
	}
	__smp_mark("SMP_INIT-post-discover");

	lapic_addr = LOCAL_APIC_DEF_ADDR;
	ioapic_enabled = 0;

	tss_init_all();
	__smp_mark("SMP_INIT-post-tss_init_all");

	bsp_cpu_id = apicid2cpuid[apicid()];

	if (!lapic_enable(bsp_cpu_id)) {
		__smp_mark("SMP_INIT-bsp_lapic_FAILED");
		printf("ERROR : failed to initialize BSP Local APIC\n");
		goto uniproc_fallback;
	}
	__smp_mark("SMP_INIT-post-lapic_enable");

	bsp_lapic_id = apicid();

	/*
	 * acpi_init() was already called in cstart on amd64.  Calling it again
	 * here is harmful because the original RSDT physical bytes (along with
	 * the MADT) have been clobbered by pg_alloc_page handing the pages out;
	 * a second parse would corrupt the still-valid sdt_trans[] and S5 state
	 * with garbage.  See acpi.c for the MADT snapshot rationale.
	 */

	if (!detect_ioapics()) {
		__smp_mark("SMP_INIT-no_ioapic");
		lapic_disable();
		lapic_addr = 0x0;
		goto uniproc_fallback;
	}
	__smp_mark("SMP_INIT-post-detect_ioapics");

	ioapic_enable_all();
	__smp_mark("SMP_INIT-post-ioapic_enable_all");

	if (ioapic_enabled)
		machine.apic_enabled = 1;

	/* Set SMP IDT entries. */
	apic_idt_init(0); /* not a reset */
	idt_reload();
	__smp_mark("SMP_INIT-post-idt_reload");

	BOOT_VERBOSE(printf("SMP initialized for %u CPUs (BSP only — AP "
	    "trampoline is a first-draft scaffold, not yet enabled)\n", ncpus));

	__smp_mark("SMP_INIT-pre-start_aps");
	smp_start_aps();
	__smp_mark("SMP_INIT-post-start_aps");

	/*
	 * BKL contention deadlock — see notes in smp_ipi_sched_handler
	 * (smp.c) and arch_clock.c context_stop().  The AP-side IPI
	 * handler stalls in context_stop()'s BKL_LOCK after ~46 init
	 * bounces; BSP fails to release BKL in time.  Clamp ncpus=1 so
	 * userland boots until this is resolved.
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
	/* DBG: emit '>' before sending and the dest cpu digit. */
	__smp_com1('>');
	__smp_com1('0' + (cpu & 0xf));
	apic_send_ipi(APIC_SMP_SCHED_PROC_VECTOR, cpu, APIC_IPI_DEST);
}
