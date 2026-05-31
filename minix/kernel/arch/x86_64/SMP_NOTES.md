# amd64 SMP — implementation notes

State: **functional**.  `-smp 2` boots cleanly to the getty login
prompt on QEMU.  The `ncpus = 1` clamp that previous sessions added
to `arch/x86_64/arch_smp.c` (right after `smp_start_aps()`) is no
longer needed and has been removed.

## The bug that gated everything

`smp_schedule_sync()` in `kernel/smp.c` stored the target process
pointer as `(u32_t) p` even though the storage field is `uintptr_t`.
On i386 this is a no-op widening; on amd64 it silently truncated
64-bit kernel-virtual proc pointers (~`0xFFFFFFFF8XXXXXXX`) to 32
bits, and the receiving CPU dereferenced into bogus memory the
first time any cross-CPU FPU-save / proc-stop request fired —
which happens almost immediately, during init's first fork.

Fix is a one-line cast change.  Commit acdc2c942.

Past sessions chased this as a "structural BKL deadlock" and built
several layers of workaround (per-CPU bkl_held_by_cpu flag,
conditional BKL_UNLOCK, context_stop_idle skip path).  The
workarounds were sound on their own, but the real bug was the
pointer truncation; the workarounds couldn't help because the AP
was page-faulting silently inside `smp_sched_handler` before it
could ever clear the sched_ipi_data flags BSP was waiting on.

## What's in place now

- Trampoline (real-mode → long-mode → `smp_ap_boot`) in
  `trampoline.S` with hand-encoded `66 67 ff 2d disp32` far-jump
  (gas in `.code16` emits only the 66h prefix and silently produces
  a real-mode `jmp [DI]` that lands in garbage).
- Per-CPU AP init in `ap_finish_booting()` — kernel GDT load, IDT
  reload, LTR for this CPU's TSS, `ap_set_kernel_gs_base()`,
  `ap_setup_syscall_msrs()`, `cpu_enable_features()`.
- TSC-deadline-aware AP wait loop in `smp_start_aps()` (i386's
  LAPIC_TIMER_CCR poll doesn't work on amd64 when TSC-deadline mode
  is active).
- Per-CPU `bkl_held_by_cpu` flag (`kernel/smp.c` + `kernel/smp.h`)
  with conditional `BKL_UNLOCK()` macro in `kernel/spinlock.h`.
- `context_stop_idle()` (`arch_clock.c`) nested-IPI path: detects
  whether this CPU already holds BKL.  If yes, accounts inline and
  calls `smp_sched_handler()` with the already-held lock.  If no
  (AP was halted in idle), takes BKL, processes, releases.  Either
  way the BKL state on return matches the state on entry.

## Things that turned out NOT to be the problem

- **Double-EOI in `smp_ipi_sched_handler`** — i386 has the same
  pattern and i386 SMP works.
- **cpuid macro reading wrong offset** — was fixed but wasn't the
  blocker.  Genuine bug from amd64 storing cpu id as `reg_t` (8 b)
  not `u32_t` (4 b).
- **com1_byte clobbering AL between movabs and jmp** — also a real
  bug but only affected debug output, not control flow.
- **Lost-wakeup race in enqueue()** — the `cpu_is_idle` gate may
  still be racy in theory but doesn't matter in practice on amd64.
- **"Structural BKL deadlock"** — was the previous hypothesis after
  a 25-minute test showed only 87 IPIs total.  Wrong direction —
  AP was alive but page-faulting silently on the truncated pointer.

## Debug infrastructure currently in tree

Single-character raw-COM1 markers, retained because they were
invaluable for diagnosing this bug and will be again:

| Marker | Where | Meaning |
|--------|-------|---------|
| `R g G P C E M L D S J` | `trampoline.S` | trampoline stages |
| `b` | `smp_ap_boot()` | AP entered C land |
| `f 1 g i t s y 2 3 4 5 e 6 7 8 9 !` | `ap_finish_booting()` | per-CPU init |
| `SMP_INIT-* START_APS-* BSP-finish-*` | various | high-level boot |
| `>N` | `arch_send_smp_schedule_ipi` | BSP sends sched IPI to CPU N |
| `<N` | `smp_ipi_sched_handler` | CPU N entered C IPI handler |
| `E<endpoint>:<name>` | `enqueue` (cross-cpu) | proc being woken |
| `@` | `switch_to_user` | AP picked non-idle proc |
| `%` | `arch_finish_switch_to_user` | AP about to iretq to user |

These should come out before any final upstream-style merge but
are worth keeping while we're still validating SMP correctness on
heavier workloads (>2 CPUs, real apps, etc.).

## How to test SMP boot

```sh
sh ./build.sh -m amd64 -j24 -O ../build.amd64 -U distribution \
  && OBJ=../build.amd64 bash ./releasetools/amd64_cdimage.sh

timeout 180 qemu-system-x86_64 --enable-kvm -cpu kvm64 -m 256 -smp 2 \
    -cdrom minix_amd64.iso -device pci-ohci,id=ohci1 \
    -nographic -serial file:/tmp/serial.log -monitor null

grep -c login: /tmp/serial.log    # expect 1
```

## Still to do

- Validate `-smp 4` and `-smp 8` (only `-smp 2` exercised so far).
- Bench/measure: is there real parallelism, or do APs spend most of
  their time waiting on BKL?  The handler-WITH-BKL design means
  cross-CPU IPC is still serialized; longer-term option-4 work
  (finer-grained locks) is still relevant for performance, just no
  longer needed for correctness.
- Strip debug markers before any clean SMP merge.
- Re-verify i386 SMP didn't regress.
