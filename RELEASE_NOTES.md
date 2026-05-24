# MINIX 3 — Release Notes (davmont fork)

**Base:** upstream MINIX 3 @ `4db99f4` (November 2018)
**Period:** March 2026 – April 2026
**Commits since fork:** 245
**Branch:** `master`

---

## Highlights

This release delivers the most significant architectural change in this fork's
history: a full **x86-64 (amd64) kernel port**, accompanied by a broad sweep
of security hardening, driver additions, performance work, and test coverage
improvements. The system can now be built and run natively on 64-bit hardware.

---

## New Features

### AMD64 / x86-64 Port

The single largest body of work. MINIX now builds and runs on 64-bit x86
hardware, joining the existing i386 and ARM targets.

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
  cleanly separated while ensuring `/usr/mdec` is populated correctly.

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

### Networking

- **lwIP 64-bit alignment** — `MEM_ALIGNMENT` corrected for LP64, fixing
  potential heap corruption on 64-bit targets.
- **Raw socket broadcast fix** — spurious broadcast check removed from raw
  socket receive path in lwIP.
- **`LABEL_MAX` decoupling** — hardcoded constant removed from lwIP `ndev.c`;
  now uses the proper system definition.
- **Ethernet constants cleanup** — `NDEV_ETH_PACKET_*` constants moved from
  `minix/const.h` to `minix/netdriver.h` where they belong.

### VFS

- **`select` in device revocation** — missing select check implemented in the
  device revocation path, preventing a class of select-after-revoke issues.

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
`readtag`, `my_strtok`, `test_evbuffer_iterative`.

---

## Test Coverage

New tests added:

- **`dup2()` system call** — comprehensive test matrix covering error cases,
  descriptor aliasing, and CLOEXEC behaviour.
- **ISC event timer library** — `evConsTime`, `evAddTime` (zero and overflow),
  `evSubTime`, `evNowTime` added to `t_ev_timers.c`.
- **`hgfs_closedir`** — unit tests for the libhgfs directory close path.
- **`cd9660_valid_a_chars`** — boundary tests for the ISO 9660 character
  validation function in makefs.

---

## Build System

- **GCC 15 compatibility** — `-std=gnu11` added to `HOST_CFLAGS` in
  `tools/compat`.
- **LLVM enabled by default** — LLVM/clang is now the default compiler; C++
  tests disabled until the C++ runtime is fully wired; `lgcc_eh` linking
  refined across the tree.
- **NetBSD 10 tools sync** — host toolchain utilities updated to the NetBSD 10
  branch, picking up bug fixes in `nbmake`, `nbpax`, and related tools.
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
  deferred to avoid spurious invocations at startup.

---

## Documentation

- **`README.md`** — initial project README added.
- **`releasetools/EFI_BOOT_PLAN.md`** — detailed five-phase plan for adding
  EFI/UEFI boot support (GRUB-based, targeting both HDD and CD images).

---

## Known Gaps and In-Progress Work

| Area | Status |
|------|--------|
| **EFI boot** | Plan documented (`EFI_BOOT_PLAN.md`). Phase 1 (GRUB build fix) not yet implemented. No EFI-bootable images produced yet. |
| **Intel igc driver** | Initial send/receive only. No interrupt coalescing, no multi-queue, no ethtool-equivalent statistics. |
| **EHCI USB (amd64)** | Phase 1 only. isochronous transfers and suspend/resume not implemented. |
| **Multiboot 2.0** | Kernel speaks Multiboot 1.0 only. Upgrading would give a clean EFI memory map and system table pointer when booted via GRUB EFI. |
| **UEFI runtime services** | No `EFI_RUNTIME_SERVICES` integration. Shutdown and reboot rely on ACPI/BIOS paths only. |
| **amd64 USB OHCI/UHCI** | EHCI Phase 1 landed; OHCI and UHCI controllers not yet wired on amd64. |
| **Secure Boot** | Not planned for this release. Requires shim + signed GRUB. |
