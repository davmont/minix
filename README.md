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

The headline change is a new **x86-64 (amd64) kernel port** that boots to a
login prompt under QEMU, with IDE storage, serial console, ACPI/APIC, and
support for process address spaces above 2 GB. Other highlights since the
upstream base:

- **Modern CPU features** — NX/XD enforcement, XSAVE/AVX FPU context, SSE4 /
  AVX2 / AES-NI / RDRAND / x2APIC / PCID detection, SMP limit raised to 32.
- **New drivers** — Intel I225/I226 2.5 GbE (`igc`), EHCI USB host (Phase 1),
  OHCI scaffolding (Phase 3a), multi-controller `usbd`.
- **Security hardening** — tree-wide audit replacing unsafe `strcpy` /
  `sprintf` / `system()` patterns; buffer-overflow fixes in `kgets`, `backup`,
  `fdisk`, `minix-service`; ELF auxvec UID/GID populated correctly.
- **Performance** — 30+ O(N²) `strlen`-in-loop patterns eliminated across the
  tree.
- **Build & toolchain** — GCC 15 compatibility, LLVM/clang by default,
  NetBSD 10 host-tools sync, C++ ATF and `kyua-cli` tests re-enabled.
- **Test coverage** — new ATF tests for `dup2`, libc string functions, the
  ISC event timer library, `hgfs`, `cd9660`, and PM `ptrace`.

For the full change log, see [`RELEASE_NOTES.md`](RELEASE_NOTES.md).

---

## Known Limitations

| Area | Status |
|------|--------|
| **amd64 SMP** | AP bring-up is disabled; kernel runs single-core (`ncpus = 1`). The i386 SMP path is unaffected. |
| **EFI / UEFI boot** | Not yet functional. A five-phase implementation plan is documented in `releasetools/EFI_BOOT_PLAN.md`. The existing GRUB scaffolding in the release scripts is disabled (`EFI_SIZE=0`) and references a non-existent GRUB `minix3` module. BIOS boot works normally. |
| **Intel igc driver** | Initial send/receive only. No interrupt coalescing, multi-queue, or statistics support yet. |
| **EHCI USB (amd64)** | Phase 1 only. Isochronous transfers and suspend/resume not implemented. |
| **amd64 USB OHCI** | Phase 3a scaffolding only (PCI discovery + DMA pools). Transfer scheduling not yet wired. |
| **amd64 USB UHCI** | Not yet wired. |
| **Multiboot 2.0** | Kernel speaks Multiboot 1.0 only. Multiboot 2.0 would provide a clean EFI memory map and system table pointer when booting via GRUB EFI. |
| **UEFI runtime services** | No `EFI_RUNTIME_SERVICES` integration. Shutdown and reboot rely on ACPI/BIOS paths only. |
| **Secure Boot** | Not supported. Requires a signed shim and signed GRUB image. |

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
