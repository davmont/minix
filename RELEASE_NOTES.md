# MINIX 3 — Release Notes (davmont fork)

**Base:** upstream MINIX 3 @ `4db99f4` (November 2018)
**Period:** March 2026 – May 2026
**Commits since fork:** 352
**Branch:** `devel`

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

**SMP — temporarily single-core**

- `smp_start_aps()` is still `#if 0`'d on amd64. The kernel now clamps
  `ncpus` to 1 so userspace doesn't get `EBADCPU` trying to schedule init
  on never-booted APs. Multi-core bring-up is the next step.

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
- **`releasetools/EFI_BOOT_PLAN.md`** — detailed five-phase plan for adding
  EFI/UEFI boot support (GRUB-based, targeting both HDD and CD images).

---

## Known Gaps and In-Progress Work

| Area | Status |
|------|--------|
| **amd64 SMP** | `smp_start_aps()` still `#if 0`'d; kernel clamps `ncpus = 1`. Multi-core bring-up is the next step on amd64. |
| **EFI boot** | Plan documented (`EFI_BOOT_PLAN.md`). Phase 1 (GRUB build fix) not yet implemented. No EFI-bootable images produced yet. |
| **Intel igc driver** | Initial send/receive only. No interrupt coalescing, no multi-queue, no ethtool-equivalent statistics. |
| **EHCI USB (amd64)** | Phase 1 only. Isochronous transfers and suspend/resume not implemented. |
| **amd64 USB OHCI** | Phase 3a scaffolding landed (PCI discovery + DMA pools). Transfer scheduling not yet wired. |
| **amd64 USB UHCI** | Not yet wired. |
| **Multiboot 2.0** | Kernel speaks Multiboot 1.0 only. Upgrading would give a clean EFI memory map and system table pointer when booted via GRUB EFI. |
| **UEFI runtime services** | No `EFI_RUNTIME_SERVICES` integration. Shutdown and reboot rely on ACPI/BIOS paths only. |
| **Secure Boot** | Not planned for this release. Requires shim + signed GRUB. |
