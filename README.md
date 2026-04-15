MINIX 3
=======

MINIX 3 is a free, open-source operating system designed to be highly reliable,
flexible, and secure. Unlike traditional monolithic kernels (like Linux or
Windows) where a single driver crash can bring down the entire system, MINIX 3
is built on a tiny microkernel architecture.

The project's primary goal is to create a "self-healing" system that can detect
and repair its own faults on the fly without user intervention.

> **Note:** This repository is a fork maintained by David Montero and is NOT
> maintained by the Stichting MINIX Research Foundation. See
> [What's New](#whats-new-in-this-fork) for changes relative to upstream.

---

## Key Features

- **Fault Tolerance** — Most of the OS runs as isolated user-mode processes. If
  a driver crashes, it is automatically restarted by the Reincarnation Server
  without affecting the rest of the system.
- **Microkernel Design** — The kernel itself is only about 6,000 lines of
  executable code, making it easier to verify and secure.
- **POSIX Compliant** — Supports a wide range of standard UNIX software.
- **NetBSD Compatibility** — Uses the NetBSD pkgsrc package management system,
  giving access to thousands of prebuilt packages.
- **Multi-Architecture** — Runs on x86 (IA-32), x86-64 (amd64), and ARM.
- **SMP Support** — Symmetric multi-processing up to 32 CPUs.
- **Educational Foundation** — Originally created by Andrew S. Tanenbaum; a
  world-class resource for learning OS design and implementation.

---

## Architecture

MINIX 3 is structured in four layers:

| Layer | Name | Responsibility |
|-------|------|----------------|
| 1 | Microkernel | Interrupts, scheduling, message passing |
| 2 | Device Drivers | Run in user space (disk, network, USB, ...) |
| 3 | Server Processes | VFS, Process Manager, Reincarnation Server |
| 4 | User Programs | Shells, editors, applications |

---

## What's New in This Fork

### AMD64 / x86-64 Port

MINIX now builds and runs natively on 64-bit x86 hardware. The port covers
the full kernel stack: 4-level page tables, long mode entry, 64-bit GDT/IDT,
LP64 ABI, and a working cross-toolchain (`x86_64-elf64-minix`). Distribution
builds (`build.sh -m amd64 ... release`) produce bootable amd64 images.

The i386 BIOS bootloaders (`boot_monitor`, `bootxx_cd9660`,
`bootxx_minixfs3`) are built as a separate step after the main amd64 tree,
keeping the architecture trees cleanly separated.

### Kernel CPU Feature Extensions

- **NX/XD bit** — `EFER.NXE` enabled at boot on all CPUs, enforcing
  no-execute protection on data pages.
- **XSAVE/AVX** — FPU context save upgraded from `FXSAVE` to the `XSAVE`
  family, preserving full AVX register state across context switches.
- **Modern CPUID detection** — SSE4, AVX2, AES-NI, POPCNT, BMI1/2, RDRAND,
  FSGSBASE, TSC-deadline, x2APIC, and PCID all detected and exposed to the
  kernel.
- **x86-64-v2 compiler optimisation** — when the build host supports it,
  `-march=x86-64-v2` is selected automatically for faster generated code.
- **SMP limit raised** — maximum CPU count increased from 8 to 32.

### New and Updated Drivers

- **Intel igc 2.5 GbE** (`net/igc`) — initial driver for Intel I225/I226
  Ethernet controllers.
- **EHCI USB host controller** — USB 2.0 high-speed host support added for
  amd64. PCI support headers extended for amd64, arm, and i386.
- **Multi-controller USB architecture** — `usbd` refactored to manage EHCI,
  OHCI, and UHCI controllers simultaneously through a unified registry.

### Security Hardening

A systematic audit of unsafe C patterns across the tree:

- `strcpy` → `strlcpy` in DS server, Reincarnation Server, syslogd, makefs,
  mtree, cd9660, profile, debug, untgz, LPdir, v7fs, and others.
- `sprintf` → `snprintf` in crontab, `at`, and makefs cd9660 logic.
- Shell injection via `system()` closed in `devmand` and `sz` (replaced with
  `fork`/`exec`/`waitpid`).
- Buffer overflow in `kgets()` fixed.
- Length check added to `makefs` bootimagedir handling.

### Performance

Over 30 O(N²) `strlen`-in-loop patterns eliminated across the codebase,
including in `rtadvd`, `svrctl`, `ftp`, `libarchive`, `isoread`,
`libcurses/ex2`, `writeisofs`, `openssldh`, and the `__parse_cap` debug loop.

### Networking

- lwIP aligned for 64-bit targets (`MEM_ALIGNMENT` fix).
- Raw socket spurious broadcast check removed.
- `NDEV_ETH_PACKET_*` constants moved to `minix/netdriver.h`.

### Tools and Utilities

- **`trace`** — argument printing added for `TIOCMSET` and related char-device
  ioctls.
- **`syslogd`** — leading whitespace stripped before the log facility field;
  Base64 length made exact.
- **VFS** — missing `select` check added to the device revocation path.
- **MIB server** — `MIB_FLAG_AUTHED` flag for authenticated `lsys` calls.
- **Build system** — GCC 15 compatibility (`-std=gnu11`); LLVM/clang enabled
  by default; NetBSD 10 host tools sync; `COMPAT_MAGIC` named constant.

### Test Coverage

New tests added for: `dup2()`, ISC event timer library (`evConsTime`,
`evAddTime`, `evSubTime`, `evNowTime`), `hgfs_closedir`, and
`cd9660_valid_a_chars` boundary conditions.

---

## Known Limitations

| Area | Status |
|------|--------|
| **EFI / UEFI boot** | Not yet functional. A five-phase implementation plan is documented in `releasetools/EFI_BOOT_PLAN.md`. The existing GRUB scaffolding in the release scripts is disabled (`EFI_SIZE=0`) and references a non-existent GRUB `minix3` module. BIOS boot works normally. |
| **Intel igc driver** | Initial send/receive only. No interrupt coalescing, multi-queue, or statistics support yet. |
| **EHCI USB (amd64)** | Phase 1 only. Isochronous transfers and suspend/resume not implemented. OHCI and UHCI not yet wired on amd64. |
| **Multiboot 2.0** | Kernel speaks Multiboot 1.0 only. Multiboot 2.0 would provide a clean EFI memory map and system table pointer when booting via GRUB EFI. |
| **UEFI runtime services** | No `EFI_RUNTIME_SERVICES` integration. Shutdown and reboot rely on ACPI/BIOS paths only. |
| **Secure Boot** | Not supported. Requires a signed shim and signed GRUB image. |
| **amd64 USB OHCI/UHCI** | EHCI Phase 1 landed; OHCI and UHCI controllers not wired on amd64. |

---

## Getting Started

### Prerequisites

For the best experience, run MINIX 3 inside a virtual machine.

- **Hypervisor:** VirtualBox, VMware, or QEMU
- **Image:** build from source (see below) or download from the upstream
  [Official Downloads Page](https://wiki.minix3.org)

### Building from Source

```sh
# i386 (original upstream target)
sh build.sh -j4 -mi386 -O ../obj.i386 -D ../obj.i386/destdir.i386 -U -u release
bash releasetools/x86_cdimage.sh

# amd64 (this fork)
sh build.sh -j4 -mamd64 -O ../obj.amd64 -D ../obj.amd64/destdir.amd64 -U -u release
bash releasetools/amd64_cdimage.sh
```

### Running in QEMU (BIOS)

```sh
# amd64 ISO
qemu-system-x86_64 --enable-kvm -cdrom minix_amd64.iso

# amd64 HDD image
qemu-system-x86_64 --enable-kvm -m 256M -hda minix_amd64.img
```

### Installation (from ISO)

1. Create a VM (256 MB RAM, 2 GB+ HDD).
2. Mount the ISO and boot.
3. Log in as `root` (no password).
4. Run `setup` and follow the prompts.
5. Power off, remove the ISO, reboot.

For detailed instructions see the
[MINIX 3 Installation Guide](https://wiki.minix3.org/doku.php?id=usersguide:doinginstallation).

---

## Documentation

- **Official Wiki:** [wiki.minix3.org](https://wiki.minix3.org)
- **User Guide:** [Getting Started](https://wiki.minix3.org/doku.php?id=www:getting-started:start)
- **Release Notes:** [`RELEASE_NOTES.md`](RELEASE_NOTES.md) — detailed changelog since the fork
- **EFI Boot Plan:** [`releasetools/EFI_BOOT_PLAN.md`](releasetools/EFI_BOOT_PLAN.md)
- **Book:** *Operating Systems: Design and Implementation* (3rd ed.) — Tanenbaum & Woodhull

---

## Contributing

Pull requests are welcome. Whether you are fixing bugs, porting drivers, or
improving documentation:

- Source tree is at `/usr/src` on a running MINIX system.
- Please follow existing BSD code style.
- Prefer `strlcpy`/`strlcat` over `strcpy`/`strcat`; `snprintf` over `sprintf`.
- For security-sensitive changes, reference the relevant CWE in the commit
  message.

---

## License

MINIX 3 is released under the **BSD-3-Clause License**. See the `LICENSE` file
for details.
