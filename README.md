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

The headline change is a working **x86-64 (amd64) kernel port** that boots
to a login prompt on both legacy BIOS and UEFI firmware, with SMP, an IPC
fastpath, modern CPU features, and most NetBSD userland tools intact.
Other highlights since the upstream base:

- **amd64 SMP** — full AP bring-up, per-CPU GDT/IDT/TSS, BKL serialization,
  validated on `-smp 2/4/8` up to 32 CPUs. (See the "amd64 subsystem state"
  table below.)
- **IPC fastpath + colocation** — SENDREC same-CPU fastpath plus a Tier 1
  scheduler that migrates servers to their dominant client's CPU; **4.86×**
  SMP IPC throughput improvement on `-smp 4` (p50 78k → 16k cycles).
- **Hybrid BIOS + UEFI boot** — single CD image boots on either firmware via
  El Torito with a GRUB-built EFI System Partition. Multiboot 2.0 + ACPI
  RSDP handoff implemented; linear-framebuffer text console for headless
  UEFI environments.
- **Modern CPU features** — NX/XD enforcement, XSAVE/AVX FPU context, eager
  FPU dispatch on amd64, SSE4 / AVX2 / AES-NI / RDRAND / x2APIC / PCID
  detection.
- **New drivers** — Intel I225/I226 2.5 GbE (`igc`), EHCI USB host (Phase 1),
  OHCI scaffolding (Phase 3a), multi-controller `usbd`, AHCI / legacy IDE
  auto-detect at boot.
- **Allocator** — switched libc to `jemalloc`.
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

## amd64 Subsystem State

A snapshot of where each piece of the amd64 port stands. ✓ = production-grade,
↻ = working with caveats, ⌛ = in progress, ✗ = not implemented.

| Subsystem | Status |
|-----------|--------|
| Boot (legacy BIOS) | ✓ |
| Boot (UEFI / GPT, q35) | ✓ via hybrid El Torito; Multiboot 2.0 handoff |
| SMP | ✓ up to 32 CPUs; validated on `-smp 2/4/8` |
| IPC fastpath + Tier 1 colocation | ✓ 4.86× SMP win (p50 78k → 16k cycles) |
| FPU / SSE | ✓ eager dispatch |
| Storage (AHCI + IDE) | ✓ auto-detect + graceful fallback |
| Console (serial) | ✓ |
| Console (linear framebuffer) | ✓ for UEFI / GOP environments |
| ACPI / APIC | ✓ |
| Intel `igc` 2.5 GbE | ↻ basic send/receive; no coalescing/multi-queue/stats |
| USB EHCI (USB 2.0) | ↻ Phase 1 transfer model; no isoch, no suspend/resume |
| USB OHCI (USB 1.1) | ⌛ Phase 3a scaffolding (PCI + DMA pools); transfer scheduling next |
| USB UHCI | ✗ |
| USB xHCI (USB 3.x) | ✗ |
| UEFI runtime services | ✗ shutdown/reboot via ACPI only |
| Secure Boot | ✗ |

---

## Known Limitations

| Area | Status |
|------|--------|
| **Intel igc driver** | Initial send/receive only. No interrupt coalescing, multi-queue, or statistics support yet. |
| **EHCI USB (amd64)** | Phase 1 only. Isochronous transfers and suspend/resume not implemented. |
| **amd64 USB OHCI** | Phase 3a scaffolding only (PCI discovery + DMA pools). Transfer scheduling next. |
| **amd64 USB UHCI / xHCI** | Not yet wired. |
| **UEFI runtime services** | No `EFI_RUNTIME_SERVICES` integration. Shutdown and reboot rely on ACPI paths only. |
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
sh build.sh -j4 -mi386 -O ../obj.i386 -U release
bash releasetools/x86_cdimage.sh

# amd64 (this fork) — replace 24 with your CPU core count
sh build.sh -m amd64 -U -j24 -O ../obj.amd64 release
OBJ=../obj.amd64 bash releasetools/amd64_cdimage.sh
```

The build produces `minix_amd64.iso` in the current directory.

### Running in QEMU

> **Important — `-cpu host` is required for amd64.**
> MINIX uses the `WRFSBASE` instruction for TLS setup; QEMU's default `kvm64`
> CPU model does not expose that feature (CPUID leaf 7 EBX bit 0), so the
> kernel will panic at boot without it.

```sh
# amd64 — bootloader path (recommended)
qemu-system-x86_64 --enable-kvm -cpu host -smp 4 -m 256 \
    -cdrom minix_amd64.iso

# amd64 — direct kernel path (faster iteration)
qemu-system-x86_64 --enable-kvm -cpu host \
    -kernel ../obj.amd64/minix/kernel/kernel \
    -append "bootcd=1 cdproberoot=1" \
    -cdrom minix_amd64.iso

# i386
qemu-system-i386 --enable-kvm -m 256 -cdrom minix_i386.iso

# amd64 HDD image
qemu-system-x86_64 --enable-kvm -cpu host -m 256 -hda minix_amd64.img
```

### Installation (from ISO)

1. Create a VM (256 MB RAM, 2 GB+ HDD).
2. Mount the ISO and boot (use `-cpu host` for amd64 in QEMU).
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
