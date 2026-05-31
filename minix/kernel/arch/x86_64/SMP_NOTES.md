# amd64 SMP — diagnostic notes

State: not yet shippable.  `arch/x86_64/arch_smp.c` clamps `ncpus = 1`
right after `smp_start_aps()` because exposing APs to the scheduler
deadlocks userspace.  The clamp can be removed only after BKL
contention is addressed (see "Real fix" below).

This file collects everything learned across sessions 1-10 of the
SMP-AP bringup work so the next developer doesn't have to rediscover
it.

## What works today

- Trampoline (real-mode → long-mode → `smp_ap_boot`) — `trampoline.S`
  with hand-encoded `66 67 ff 2d disp32` far-jump (gas in `.code16`
  emits only 66h, missing 67h; the resulting ModRM 2d means `[DI]`
  in real mode → garbage).
- Per-CPU AP init in `ap_finish_booting()` — kernel GDT load, IDT
  reload, LTR for this CPU's TSS, `ap_set_kernel_gs_base()`,
  `ap_setup_syscall_msrs()` (EFER.SCE+STAR+LSTAR+FMASK),
  `cpu_enable_features()`.
- TSC-deadline-aware AP wait loop in `smp_start_aps()` — the i386-style
  `while (lapic_read(LAPIC_TIMER_CCR))` doesn't work on amd64 when
  TSC-deadline mode is in use; replaced with TSC-based 500 ms timeout.
- Per-CPU `bkl_held_by_cpu` flag (in `kernel/smp.c` + `kernel/smp.h`)
  with conditional `BKL_UNLOCK()` macro (in `kernel/spinlock.h`).
  Lets the nested-IPI path safely skip `BKL_LOCK` without the
  trailing `context_stop(KERNEL) -> BKL_UNLOCK` releasing a lock the
  CPU never acquired.
- `context_stop_idle()` (in `arch_clock.c`) skips `context_stop()`
  call when the AP isn't holding BKL, doing time accounting inline.

## What doesn't work, and why

When `ncpus = 1` clamp is removed and APs are exposed to userland
scheduling, init bounces between AP and BSP for ~46 cycles then the
boot stops progressing.  Login never appears.

The bottleneck is **BKL contention magnitude on amd64**.  Same code
runs on i386 with `-smp 2` and reaches login fine (commit 586521031
verified after the i386 catch-up build fixes).  The difference is
purely in kernel path lengths:

- amd64 SYSCALL/SYSRET + swapgs + IA32_KERNEL_GS_BASE is longer than
  i386 sysenter/int 0x80.
- amd64 4-level page table walks are deeper than i386 2-level.
- amd64 XSAVE state save/restore is bigger than i386 FXSAVE.
- amd64 process-context-identifiers (PCID) add CR3 work.

The cumulative effect: every kernel entry holds BKL for ~2× longer
on amd64 than on i386.  With BSP+APs all contending the single BKL,
APs eventually starve.  i386 escapes because BSP releases BKL fast
enough for APs to get fair share.

## Things tried (and ruled out)

- **Double-EOI in `smp_ipi_sched_handler`** — `lapic_intr` macro calls
  `arch_eoi` after the handler, and `smp_ipi_sched_handler` also calls
  `ipi_ack()`.  Removing one didn't help; i386 has the same pattern
  and i386 SMP works.
- **`cpuid` macro reading wrong offset on amd64** — was reading
  `[-1]` of a `u32_t *` (= top - 4) but `tss_init` writes the cpu id
  as `reg_t` (8 bytes) at top - 8.  Fixed in `include/arch_smp.h` to
  read `[-2]`.
- **`com1_byte` clobbering AL between `movabs` and `jmp *(%rax)`** —
  in `long_mode_entry`, debug marker after `movabs $ap_entry_kvirt,
  %rax` then `jmp *(%rax)` corrupted RAX's low byte.  Fixed by
  putting the marker before the movabs and using a register-direct
  jump (`movabs $_C_LABEL(smp_ap_boot), %rax; jmp *%rax`) since
  ap_entry is always smp_ap_boot anyway.
- **Skipping `BKL_LOCK` in `context_stop_idle`** without the
  per-CPU flag — broke BKL pairing (the trailing
  `context_stop(KERNEL)` BKL_UNLOCK released BSP's lock).  Fixed by
  the `bkl_held_by_cpu` flag.
- **Lost-wakeup race in `enqueue()`** — `else if
  (get_cpu_var(rp->p_cpu, cpu_is_idle))` gates IPI on cpu_is_idle;
  classic race window between AP's `pick_proc` returning NULL and
  setting `cpu_is_idle = 1`.  Tried always sending IPI: more IPIs,
  more BKL contention, **slower** progress (1 byte per 13 s vs 1
  per second).  Race may exist but isn't the dominant problem.

## Real fix (option 4)

Replace BKL with finer-grained locks for the subsystems init
touches most:
1. **proc table lock** — protects `struct proc` modifications
   (RTS flags, p_misc_flags, p_delivermsg).  This is the highest-
   traffic path.
2. **per-CPU run-queue lock** — already mostly per-CPU but
   `enqueue()` reaches across CPUs; needs a dispatch lock.
3. **VM/PM IPC reply lock** — server replies must reach the right
   blocked proc atomically.
4. **fpu_owner lock** — currently protected by BKL; per-CPU.

Realistic timeline: multi-session.  FreeBSD took 10+ years to mostly
remove `Giant`.  Linux took 5+.  But MINIX's kernel is small (~6 kLOC
of actually-contended code), so a focused refactor of ~3-4 locks
could be done in days, not years.

## Debug infrastructure currently in the kernel

The following single-character raw-COM1 markers are still in the
tree — invaluable for the next round of work:

| Marker | Where | Meaning |
|--------|-------|---------|
| `R g G P C E M L D S J` | `trampoline.S` | trampoline stages |
| `b` | `smp_ap_boot()` | AP entered C land |
| `f 1 g i t s y 2 3 4 5 e 6 7 8 9 !` | `ap_finish_booting()` | per-CPU init stages |
| `SMP_INIT-* START_APS-* BSP-finish-*` | various | high-level boot stages |
| `>N` | `arch_send_smp_schedule_ipi` | BSP sends sched IPI to CPU N |
| `<N` | `smp_ipi_sched_handler` | CPU N entered C IPI handler |
| `E<endpoint>:<name>` | `enqueue` (cross-cpu) | proc being woken |
| `@` | `switch_to_user` | AP picked non-idle proc |
| `%` | `arch_finish_switch_to_user` | AP about to iretq to user |

Strip these before final SMP merge.

## How to test SMP boot

```sh
# build
sh ./build.sh -m amd64 -j24 -O ../build.amd64 -U distribution \
  && OBJ=../build.amd64 bash ./releasetools/amd64_cdimage.sh

# run -smp 4 headless, capture serial
timeout 180 qemu-system-x86_64 --enable-kvm -cpu kvm64 -m 256 -smp 4 \
    -cdrom minix_amd64.iso -device pci-ohci,id=ohci1 \
    -nographic -serial file:/tmp/serial.log -monitor null

# with clamp in place: login should appear in ~180 s
# without clamp: trace ends with `45e6789!345e6789!345e6789!` + IPI cycles
grep -c login /tmp/serial.log
```

## i386 SMP for comparison

i386 SMP works with `-smp 2` (reaches login).  Use it as the
behavioural reference when comparing kernel paths.  i386 build
needed three catch-up fixes (commit 586521031); see git log.
