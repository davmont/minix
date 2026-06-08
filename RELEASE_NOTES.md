# MINIX 3 — Release Notes (davmont fork)

**Base:** upstream MINIX 3 @ `4db99f4` (November 2018)
**Period:** March 2026 – June 2026
**Commits since fork:** 421
**Branch:** `devel` (at `21c397549`)

The notes below are split into two blocks: the initial "boots to login"
work (March–May 2026) followed by an "Update — Late May / June 2026"
section that covers SMP bring-up, the IPC fastpath, hybrid UEFI boot, and
several follow-up bug fixes.  See [`README.md`](README.md) for a one-page
summary of where the amd64 port stands today.

---

## Highlights

This release brings MINIX 3 to a working **bootable x86-64 system**. Building
on the kernel port skeleton from earlier in the cycle, amd64 now boots from a
CD image all the way to a serial login prompt under QEMU, with a working IDE
disk driver, ACPI/APIC bring-up, and >4 GB physical memory support. Alongside
the boot work, the release adds more security fixes, additional strlen/perf
cleanup, new ATF tests, and Phase 3a OHCI USB scaffolding.

---

## New Features

### AMD64 / x86-64 Port — Now Boots to Login

The single largest body of work. MINIX now builds, boots, and reaches a
serial login prompt on 64-bit x86 hardware, joining the existing i386 and
ARM targets.

**Kernel skeleton and ABI**

- **Kernel skeleton** — `head.S`, `mpx.S`, `protect.c`, `pg_utils.c`: 4-level
  page tables, 64-bit GDT/IDT, long mode entry sequence.
- **Exception and system call paths** — `exception.c`, `arch_system.c`,
  `pre_init.c` ported and validated for LP64 ABI.
- **`klib.S` and boilerplate adapters** — atomic operations, memory copy
  routines, and arch-specific stubs updated for x86-64 calling conventions.
- **I/O stubs, VM management, watchdog, IPC tables** — full Phase 4 of the
  port, covering port I/O, virtual memory operations, and inter-process
  communication infrastructure.
- **LP64 ABI fixes** — `long` and pointer sizes corrected throughout kernel
  data structures; distribution build verified end-to-end (Phase 6).
- **Cross-toolchain** — `x86_64-elf64-minix` triple wired in; LLVM gold plugin
  host triplet parameterized; `build.sh` extended with `-m amd64` support.
- **Build infrastructure** — `obj.amd64/` layout, `.gitignore` updates, and
  clean-build fix for fresh machines.
- **i386 bootloaders for amd64** — `sys/arch/i386/stand` (producing
  `boot_monitor`, `bootxx_cd9660`, `bootxx_minixfs3`) is now built as a
  separate step after the main amd64 build, keeping the architecture trees
  cleanly separated while ensuring `/usr/mdec` is populated correctly. The
  i386 bootloader cross-build from an amd64 host was also fixed.

**Boot bring-up to login prompt**

- **Boot path stabilized** — multiboot magic preservation through
  `rep stosl` bootstrap clearing, missing register move in the
  interrupt context-switch path, GDT/syscall layout fixes, and VM server
  collapsed onto a single 0-1 GB page directory. Several boot panics during
  init/VM loading resolved.
- **APIC + ACPI working** — the amd64 kernel now uses the local APIC and ACPI
  bring-up code path under QEMU.
- **Eager FPU switch** — FPU context is switched on every dispatch instead
  of the legacy lazy/`CR0.TS`/`#NM` model. The lazy path re-trapped userspace
  SSE instructions in an infinite loop on amd64.
- **`ia32_msr_read` null-deref and XSAVE area size query** — both fixed
  during early bring-up.

**Storage, console, and login**

- **IDE storage on amd64** — `at_wini` IDE driver re-enabled for amd64 and
  started from the ramdisk rc, making a virtual hard disk usable.
- **Per-arch driver migration** — i386-only drivers re-enabled for amd64
  where applicable, and the migrated set packaged in `minix-base`.
- **Serial console login** — `tty00` getty enabled for headless QEMU runs.
- **Boot polish** — `STUDUMP` boot dump silenced by default; diagnostics
  gated behind `verbose=2+`; `set -e` traps in the ramdisk rc scripts
  fixed; `/var/log/{syslog,messages}` pre-created in `syslogd_precmd` so
  the daemon doesn't hit `ENOENT` on a fresh ramdisk `/tmp/log` symlink;
  noisy VM `phys_pagefault` diagnostic dropped (fired on every MMIO
  page-in such as e1000 BAR mapping); `sysdb` rc now creates `/var/log`
  alongside `/var/run` and `/var/db`; `/var` ramdisk size bumped from
  768 KB to 2048 KB for first-boot logs.

**Memory ceilings lifted**

- **`SVMCTL_PTROOT` widened to `uint64_t`** — amd64 process CR3s can now sit
  above the 32-bit boundary.
- **`VMCTL_SETADDRSPACE` unblocked for >2 GB** — process address spaces are
  no longer capped at 2 GB physical.
- **Runtime physical-memory discovery** — VM server detects RAM size at
  boot instead of using a compile-time ceiling.
- **4 GB physical ceiling lifted** in the amd64 VM glue.

**SMP — temporarily single-core** (superseded by the late-May/June work,
see the "amd64 SMP — now real" update section below)

- `smp_start_aps()` is `#if 0`'d on amd64 in this initial release. The
  kernel clamps `ncpus` to 1 so userspace doesn't get `EBADCPU` trying to
  schedule init on never-booted APs. (Multi-core bring-up landed in the
  June 2026 follow-up.)

**Distribution and release tooling**

- `distrib/sets` updated to package amd64 port artifacts and new tests;
  `minix/kernel_const.h` marked obsolete.
- `nbmakefs` bootimage platform fixed for amd64 ISO generation in
  `releasetools/amd64_cdimage.sh`.

### Kernel CPU Feature Extensions

- **NX/XD bit** — `EFER.NXE` enabled on all CPUs at boot, enforcing
  no-execute protection on data pages (`kernel/nx`).
- **XSAVE family** — FPU context save/restore upgraded from legacy `FXSAVE`
  to `XSAVE`/`XSAVEOPT`, enabling full AVX register state preservation
  (`kernel/fpu`).
- **Extended CPUID detection** — SSE4.1/4.2, AVX, AVX2, AES-NI, POPCNT, BMI1/2,
  RDRAND, FSGSBASE, TSC-deadline, x2APIC, PCID all detected and exposed
  (`kernel/cpufeature`, `kernel/compat`).
- **x86-64-v2 compiler optimization** — build-time detection enables the
  `-march=x86-64-v2` baseline (SSE4.2, POPCNT, SSSE3) when the host CPU
  supports it, giving measurably faster generated code.
- **SMP scaled up** — maximum CPU count raised from 8 to 32.

### New Drivers

- **Intel I225/I226 2.5 GbE (`net/igc`)** — initial driver for the igc NIC
  family (Intel Ethernet Controller I225/I226), covering basic send/receive
  and link management. Marked initial; tuning and full feature support are
  ongoing.
- **EHCI USB host controller (Phase 1)** — amd64 EHCI support added, enabling
  USB 2.0 high-speed host operation on 64-bit systems. PCI support headers
  extended for amd64, arm, and i386.
- **USB multi-controller architecture (Phase 2)** — `usbd` refactored to
  manage multiple simultaneous host controllers (EHCI, OHCI, UHCI) through a
  unified controller registry. Error handling for null device handles hardened.
- **OHCI scaffolding (Phase 3a)** — `usbd` gains OHCI controller scaffolding
  alongside the existing EHCI support: PCI discovery and DMA pool setup for
  OHCI. Transfer scheduling is not yet wired.
- **USB magic-number cleanup** — `hcd.c` magic constants replaced with named
  values.

### Networking

- **lwIP 64-bit alignment** — `MEM_ALIGNMENT` corrected for LP64, fixing
  potential heap corruption on 64-bit targets; later refactored to use the
  standard `__LP64__` macro.
- **Raw socket broadcast fix** — spurious broadcast check removed from raw
  socket receive path in lwIP.
- **`LABEL_MAX` decoupling** — hardcoded constant removed from lwIP `ndev.c`;
  now uses the proper system definition.
- **Ethernet constants cleanup** — `NDEV_ETH_PACKET_*` constants moved from
  `minix/const.h` to `minix/netdriver.h` where they belong.
- **`rtadvd` `add_prefix()`** — gracefully handles malloc failure instead of
  proceeding with a NULL pointer.
- **NDP delete** — struct-copy bug fixed (was reading from the parameter
  rather than the local copy).
- **Obsolete `ND_RA_FLAG_RTPREF_MASK` workaround** — removed; modern kernels
  already define it.

### VFS

- **`select` in device revocation** — missing select check implemented in the
  device revocation path, preventing a class of select-after-revoke issues.
- **`AT_RUID` / `AT_RGID` in ELF auxiliary vector** — both placeholders were
  always 0; VFS now populates them with the caller's real UID/GID, restoring
  the value `getauxval(3)` consumers expect.

### MIB Server

- **`MIB_FLAG_AUTHED`** — new flag defined and used for authenticated `lsys`
  calls, enabling the MIB server to distinguish privileged from unprivileged
  system-level queries.

---

## Security Fixes

A systematic audit replaced unsafe C patterns across the tree with their safe
equivalents.

| Component | Fix |
|-----------|-----|
| DS server (`store.c`) | `strcpy` → `strlcpy` (multiple call sites) |
| Reincarnation Server | `strcpy` → `strlcpy` |
| `profile.c` | `strcpy` → `strlcpy` |
| `debug.c` | `strcpy` → `strlcpy` |
| `syslogd.c` | `strcpy` → `strlcpy` |
| `mtree` | `strcpy` → `strlcpy` (path construction) |
| `cd9660_set_defaults` (makefs) | `strcpy` → `strlcpy` |
| `untgz.c` | `strcpy` → `strlcpy` |
| `LPdir_vms.c` | Buffer overflow fixed |
| `v7fs` progress functions | Buffer overflows fixed |
| `minigzip` | Buffer overflows fixed |
| `crontab` | `sprintf` → `snprintf` |
| `at` command | `sprintf` → `snprintf` |
| `makefs` cd9660 logic | Unsafe `sprintf` replaced |
| `kgets()` | Buffer overflow fixed |
| `devmand` | Shell injection via `system()` → `execvp()` |
| `sz.c` | Shell injection via `system()` → `fork/exec/waitpid` |
| `makefs` bootimagedir | Missing length check added |
| `newfs_udf` crclen | `uint16_t` prevents overflow |
| `backup` | Unbounded `strcpy`/`strncat` replaced with `snprintf`/`strncpy`; later `snprintf` usage refined for directory-name handling |
| `fdisk` `dpl_partitions` | Buffer overflow fixed |
| `minix-service` | Buffer overflow in command concatenation fixed |
| VFS auxvec | `AT_RUID`/`AT_RGID` populated with real UID/GID (were always 0) |
| `console` `sys_readbios` | Missing error check added during initialization |
| `zcfree` wrappers | Pass exact size to dealloc (defense against heap-corruption tooling) |

---

## Performance Improvements

O(N²) string-length patterns eliminated across the codebase. All fixes follow
the same pattern: cache `strlen()` before a loop rather than recomputing it on
every iteration.

Affected components: `libcurses/ex2.c`, `rtadvd/config.c`, `svrctl`,
`ftp` (`complete_ambiguous`, `complete_command`, `complete_remote`),
`libarchive` test suite, `h_quota2_tests`, `parse-name-test`, `tar` tests,
`cpio` test runner, `isoread`, `openssldh` BN_fromhex, `MagicPass.cpp`,
`s_client` PSK validation, `writeisofs`, `__parse_cap` debug loop,
`readtag`, `my_strtok`, `test_evbuffer_iterative`, `bftest` (blowfish),
`xtabbed.c`, `getarg` short-argument parsing, bzip2 `pad()` loop.

Additionally: `sys_now` avoids unnecessary 64-bit arithmetic on 32-bit
targets where the wider path was pure overhead.

---

## Test Coverage

New tests added:

- **`dup2()` system call** — comprehensive test matrix covering error cases,
  descriptor aliasing, and CLOEXEC behaviour.
- **ISC event timer library** — `evConsTime`, `evAddTime` (zero and overflow),
  `evSubTime`, `evNowTime` added to `t_ev_timers.c`.
- **`hgfs_closedir`** and **`hgfs error_convert`** — unit tests for the
  libhgfs directory close path and error-code translation.
- **`cd9660_valid_a_chars`** — boundary tests for the ISO 9660 character
  validation function in makefs.
- **libc string functions** — ATF tests for `wcslen`, `wcschr`, `wcsncat`,
  `strtok`/`strtok_r`, `strndup`, `stpcpy`, `rindex`, and `index(3)`.
- **PM `ptrace` test42** — re-enabled with dynamic procfs PID lookup
  instead of a hardcoded PID that was racy under SMP and fragile on
  fresh boots.
- **C++ ATF tests and `kyua-cli` tests subdir** — re-enabled in the build
  now that the C++ runtime is sufficiently functional.

---

## Build System

- **GCC 15 compatibility** — `-std=gnu11` added to `HOST_CFLAGS` in
  `tools/compat`.
- **LLVM enabled by default** — LLVM/clang is now the default compiler.
  C++ ATF tests and the `kyua-cli` tests subdir, previously disabled, are
  re-enabled now that the C++ runtime is sufficiently wired; `lgcc_eh`
  linking refined across the tree.
- **NetBSD 10 tools sync** — host toolchain utilities updated to the NetBSD 10
  branch, picking up bug fixes in `nbmake`, `nbpax`, and related tools.
- **`make -v` propagation** — `MAKEFLAGS=-v` no longer leaks the
  verbose-output flag into sub-makes (which interpret `-v` as something
  else entirely).
- **`COMPAT_MAGIC` constant** — magic number `256` replaced with a named
  constant in `rtadvd/config.c` and friends.
- **Bootstrap warning fixes** — `bootstrapload` and `bootstrapexec` initialized
  to suppress GCC uninitialized-variable warnings.

---

## Tools and Utilities

- **`trace`** — argument printing implemented for `TIOCMSET` and a family of
  char-device ioctls that previously showed no arguments.
- **`syslogd`** — leading whitespace (spaces and tabs) stripped before the log
  facility field; Base64 length calculation made exact.
- **`devmand`** — dead `VFS` worker code removed; shell injection closed.
- **`makefs`** — padding logic refactored; `bootimagedir` length validated.
- **`rtadvd`** — `COMPAT_MAGIC` named constant; `prog_clock_gettime` call
  deferred to avoid spurious invocations at startup; graceful malloc
  failure handling in `add_prefix()`.
- **`installboot` (macppc)** — `clearapplepartmap` implemented for
  `clearboot` so the Apple Partition Map is properly cleared.
- **`backup`** — multiple safety passes on path construction (see Security
  Fixes table).

---

## Documentation

- **`README.md`** — initial project README added.

---

## Update — Late May / June 2026

A second wave of amd64 work landed after the initial "boots to login"
release.  Highlights below; the in-tree notes in
`minix/kernel/arch/x86_64/SMP_NOTES.md` and the per-feature memory entries
have the long-form history.

### amd64 SMP — now real

AP bring-up scaffolding (`e51e9c0cd`), trampoline encoding, per-CPU
GDT/IDT/TSS/GS-base/SYSCALL setup (`c2f338fc0`), and finally a structural
BKL-reentrancy bug in the nested-IPI path (`dddba40c9`, `f4d2d8bb5`,
`add25f594`).  SMP now runs reliably on `-smp 2/4/8` up to the configured
ceiling of 32 CPUs.  Two real bugs surfaced and were fixed along the way:

- **`(u32_t) p` truncating 64-bit proc pointers** in `smp_schedule_sync`
  (`acdc2c942`) — silent corruption that only showed up under SMP.
- **`switch_to_user` runnability race** masked by the marker busy-waits, exposed
  once `CONFIG_SMP_VERBOSE` gated them out (`add25f594`, `5e6de1d98`).

### IPC fastpath + Tier 1 colocation

A SENDREC same-CPU fastpath plus a scheduler change that migrates system
servers to their dominant client's CPU when traffic is concentrated.
End-to-end: **p50 78.3k → 16.1k cycles on `-smp 4` (4.86×)**, with mean
35.2k → 16.9k.  PRs #157 / #158 / #159.

Three pre-existing bugs surfaced during validation:

- **Out-of-bounds counter write** when an IPC path runs with
  `caller->p_cpu >= CONFIG_MAX_CPUS` (kernel-task slots).  Guarded.
- **FPU state across lazy migration** — `sched_proc` didn't release the FPU
  on CPU change for non-runnable procs.  Fixed by releasing and clearing
  `MF_FPU_INITIALIZED` when `cpu != p->p_cpu`.
- **`pick_cpu` clobbering migration target** — `schedule_process_migrate`
  recomputed the target CPU, overriding our colocation choice.  Bypassed
  by direct `sys_schedule(...)`.

### Hybrid BIOS + UEFI boot

Single CD image now boots on both legacy BIOS and UEFI firmware via El
Torito with two boot catalog entries (`a8faf62dd`).  GRUB-built EFI System
Partition (`a8faf62dd`, `d53a1fcc5`, `3b2b02828`), Multiboot 2.0 + ACPI
RSDP handoff in the kernel (`f15a43cbb`), and a linear-framebuffer text
console for UEFI/GOP environments (`c129a7e37`) — the `tty` driver now
detects a Multiboot 2.0 framebuffer tag and renders text to it directly.

Also: AHCI present-device detection (`8239a84d1`) so q35 UEFI CD boot
works without a spinning legacy IDE.

### Allocator

libc now uses `jemalloc` (`4ee3df26f`).  Same change also fixes a
VM-front-shrink over-read that affected long-lived process heaps.

### Boot menu / storage auto-detect

The amd64 release CD now has a single "Regular MINIX 3" entry that
auto-detects AHCI vs legacy IDE via `/proc/pci` in the ramdisk rc, with a
second "serial console + verbose" entry for debugging
(`d6f37165c`, `e34d00545`, `06d40dd7a`).  The earlier three-entry menu
that required pexpect tests to send `SPACE + "2"` is gone.

### Bug fixes

- **`top` on amd64** (PR #160) — `UNAME_HARDWARE=amd64` vs `uname() = x86_64`
  mismatch resolved with an alias table; divide-by-zero in `get_cpu_ticks`
  guarded for never-seen CPUs.
- **MIB `KERN_PROC_ARGS`** (PR #159) — guard against RS-spawned drivers that
  have no user exec frame (`mp_frame_addr == 0`); was crashing `ps` on the
  default column set.
- **`mini_notify` defensive fallback** (PR #161) — converted the long-standing
  `assert(!(MF_DELIVERMSG))` at `proc.c:1565` into a rate-limited printf +
  bitmap fallback.  Prevents a kernel panic seen under sustained
  fork+exec load on `-smp 4`; bitmap path is the source of truth for
  HARDWARE/SYSTEM notifies so no data is lost.  Root cause investigation
  continues.
- **Shebang exec frame layout** (PR #163) — `libc/sys/stack_utils.c` was
  padding `fp` to 8 bytes while `minix_stack_params` rounded
  `stack_size` to 16, leaving up to 8 bytes of slack between
  `ps_strings` and the frame end.  VFS's `patch_stack` assumed
  ps_strings sat flush against the frame end; the disagreement
  silently corrupted argv/envp for some argv combinations.  Fix pads
  fp to land exactly at `frame + stack_size - sizeof(ps_strings)`.

### Build / tooling

- **`NO_DO_TOOLS` guard** (PR #162) — `BUILDTARGETS+= do-tools` is now
  gated on `!defined(NO_DO_TOOLS)` so a developer can point `build.sh`
  at an existing tooldir via `-T` and skip the host-toolchain rebuild.
  Useful when the host kernel changes (which keys the tooldir name
  via `uname -r`).
- **i386 catch-up fixes** (`a483cade9`, `586521031`) — the i386 path
  needed `cpu_enable_features()` / `fpu_get_save_size()` stubs and a few
  build adjustments after the amd64 work.

### amd64 FPU regression — resolved

The earlier "userspace `#NM` trap loop blocks all runtime testing"
blocker (see the now-resolved `amd64-boot-regression` memory) was fixed
by `ece46dae6` — switched the amd64 dispatch path to eager FPU restore at
context-switch time.  Verified `vec=7` exception count is zero across
33k+ verbose-trace lines (2026-06-04).

---

## Known Gaps and In-Progress Work

(Updated 2026-06-04.)

| Area | Status |
|------|--------|
| **Intel igc driver** | Initial send/receive only. No interrupt coalescing, no multi-queue, no ethtool-equivalent statistics. |
| **EHCI USB (amd64)** | Phase 1 only. Isochronous transfers and suspend/resume not implemented. |
| **amd64 USB OHCI** | Phase 3a scaffolding landed (PCI discovery + DMA pools). Transfer scheduling next; runtime QEMU verification now unblocked by the FPU eager-dispatch fix. |
| **amd64 USB UHCI / xHCI** | Not yet wired. |
| **`mini_notify` race** | Defensive bitmap fallback landed (PR #161); root cause not yet pinpointed. Tripwire printf will fire if it recurs in steady-state workloads. |
| **`int vsp` truncation in libc execve** | Latent — `lib/libc/sys/execve.c:23` declares `int vsp = 0` and passes `&vsp` to a `int *vsp` parameter. Currently benign because `USR_STACKTOP_COMPACT = 0x50000000` fits in int32; bites silently if the user stacktop is ever widened. |
| **UEFI runtime services** | No `EFI_RUNTIME_SERVICES` integration. Shutdown and reboot rely on ACPI paths only. |
| **Secure Boot** | Not planned for this release. Requires shim + signed GRUB. |
