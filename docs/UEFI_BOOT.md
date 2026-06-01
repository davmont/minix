# UEFI boot support for MINIX 3 / Atomix (hybrid BIOS + UEFI CD)

## Attribution

The approach implemented here is directly inspired by the public UEFI-boot
work of **Naoaki Nonaka (`nnonaka`)**:

- Repository: <https://github.com/nnonaka/minix> (branch `uefiboot`, also
  `dev-efi2`), described by its author as *"My personal MINIX branch to
  support uefi boot."*
- That branch reaches a working EFI hand-off by **reusing an existing
  multiboot-capable EFI loader (GRUB, `bootia32.efi`)** to load the MINIX
  microkernel, rather than maintaining a bespoke `.efi` application. It builds
  GPT-partitioned live images and boots the kernel via the multiboot protocol.
  (Per its README the boot reaches the kernel but a login prompt was still
  outstanding — the EFI hand-off itself is the reusable part.)

The EFI **El Torito** catalog support that makes a *hybrid bootable ISO*
possible is also `nonaka`'s upstream NetBSD work, and is already present in
this tree — preserved verbatim, with its original copyright and CVS keyword:

- `usr.sbin/makefs/cd9660/cd9660_eltorito.h`
  (`$NetBSD: cd9660_eltorito.h,v 1.6 2017/01/24 11:22:43 nonaka Exp $`) —
  defines `ET_SYS_EFI 0xef` (the EFI platform id) and the section-header
  indicators.
- `usr.sbin/makefs/cd9660/cd9660_eltorito.c` — accepts `bootimage=efi;<file>`
  and groups boot images per platform into the catalog.

Those files carry **The NetBSD Foundation** copyright and **must not be
stripped of attribution**. New files added by this feature
(`releasetools/grub.cfg`, `releasetools/efiboot.functions`, the
`amd64_cdimage.sh` changes, and this document) are original work and follow
the project's existing BSD-style licensing; they credit `nonaka` for the
design lineage in their headers.

> Honesty note: there is **no** standalone `.efi` C application in the
> `nnonaka/minix` tree to copy — the EFI loader used there is GRUB, an
> external project. This implementation therefore does not ship a hand-written
> gnu-efi/edk2 bootloader; it packages and configures GRUB, which is the
> faithful reproduction of the reference design.

## Boot critical path

```
UEFI firmware (Boot Manager)
   └─ reads the CD's El Torito boot catalog, finds the EFI entry (platform 0xEF)
      └─ loads the embedded EFI System Partition (FAT) image as a block device
         └─ executes /EFI/BOOT/BOOTX64.EFI  (or BOOTIA32.EFI on 32-bit firmware)
            = GRUB, built standalone with $prefix=/EFI/BOOT
            └─ reads /EFI/BOOT/grub.cfg from the ESP
               └─ `search --file --set=root /boot/minix_default/kernel`
                  switches root to the ISO-9660 volume
                  └─ `multiboot /boot/minix_default/kernel ...` + `module ...`
                     GRUB calls ExitBootServices() internally, then hands off
                     in 32-bit protected mode per the multiboot spec
                     └─ MINIX kernel entry (.multiboot header, head.S)
                        EAX = 0x2BADB002, EBX = multiboot_info_t *
                        └─ pre_init() / kmain() → microkernel + servers
```

Legacy BIOS boot is unchanged: the same ISO still carries the i386 BIOS
El Torito entry (`bootxx_cd9660`) as boot-catalog entry #1.

## El Torito hybrid catalog

A hybrid ISO carries **two** El Torito entries built by `makefs -t cd9660`:

| # | platform id      | image                              | emulation |
|---|------------------|------------------------------------|-----------|
| 1 | `0x00` (x86/BIOS)| `usr/mdec/bootxx_cd9660`           | none      |
| 2 | `0xEF` (EFI)     | `efiboot.img` (FAT ESP w/ GRUB)    | none      |

`makefs` groups boot images by platform and emits the section header
(`ET_INDICATOR_HEADERMORE` / `ET_INDICATOR_HEADERLAST`) automatically, so the
only change needed in the build is appending a second
`bootimage=efi;<esp.img>` to the existing `-o` option string (see
`releasetools/amd64_cdimage.sh`). The EFI image is *not* a floppy size, so it
is recorded in "no emulation" mode — exactly what UEFI requires for an ESP.

## Building

```sh
# 1. Install the GRUB EFI build tools on the build host (one-time):
#      openSUSE: zypper in grub2 grub2-x86_64-efi grub2-i386-efi
#      Debian:   apt-get install grub-efi-amd64-bin grub-efi-ia32-bin
#
# 2. Build the world/kernel as usual, then the hybrid CD:
sh releasetools/amd64_cdimage.sh
#    UEFI=auto (default) builds hybrid if GRUB is present, BIOS-only otherwise.
#    UEFI=yes forces hybrid (errors out if GRUB tools are missing).
#    UEFI=no  forces the historical BIOS-only ISO.
```

The EFI System Partition image is built with the in-tree `nbmakefs -t msdos`,
so **no host `mtools`/`mkfs.vfat` is required** — only `grub-mkimage`.

## Testing under QEMU + OVMF

```sh
# BIOS (unchanged):
qemu-system-x86_64 --enable-kvm -cdrom minix_amd64.iso

# UEFI (needs an OVMF firmware image, e.g. /usr/share/qemu/ovmf-x86_64.bin):
qemu-system-x86_64 --enable-kvm \
    -drive if=pflash,format=raw,readonly=on,file=/usr/share/qemu/OVMF.fd \
    -cdrom minix_amd64.iso
```

## Pitfalls watched for

1. **ExitBootServices() ordering.** Because GRUB owns the EFI side, it performs
   the `GetMemoryMap()` → `ExitBootServices()` dance and only then jumps to the
   kernel. The kernel must *not* assume any boot-service or runtime-service is
   live; it consumes only the multiboot `mmap_*` memory map (the same one it
   already parses for BIOS multiboot). This avoids the classic UEFI bug of
   calling firmware after boot services are gone.
2. **Map key staleness.** (Relevant only if you later write a native `.efi`.)
   `ExitBootServices()` must be called with the map key from the *immediately
   preceding* `GetMemoryMap()`; any allocation in between invalidates the key
   and the call returns `EFI_INVALID_PARAMETER`. The loop is: get map → try
   exit → on failure re-get map and retry. GRUB already does this correctly.
3. **32-bit vs 64-bit firmware.** MINIX/i386 firmware and some tablets ship
   32-bit UEFI; that is why `BOOTIA32.EFI` is built alongside `BOOTX64.EFI`.
   The multiboot hand-off is 32-bit protected mode regardless of EFI bitness,
   which is exactly what `head.S` expects, so a single kernel image serves both.

## Multiboot header placement (required kernel change)

GRUB's `multiboot` command scans only the **first 8192 bytes** of the kernel
file for the `0x1BADB002` magic (`multiboot2`: first 32 KB).  The x86 kernel
links at 2 MB and the default linker max-page-size of 2 MB padded the first
`PT_LOAD`'s *file* offset out to 2 MB, so `.multiboot` landed at file offset
`0x200000` — far past GRUB's window.  The lenient NetBSD BIOS loader coped, but
GRUB failed with `no multiboot header found`.

Fix (committed in `minix/kernel/Makefile`): link x86 kernels with
`-Wl,-z,max-page-size=0x1000`.  This changes only ELF *file* layout —
`p_paddr`/`p_vaddr` are unchanged — so BIOS boot is unaffected.  The magic
moves to file offset `0x1000` and the image shrinks ~2.6 MB → ~0.3 MB.

## Verification status (QEMU 10.2 + edk2/OVMF, KVM)

Verified end to end with this branch:

| Stage | BIOS (SeaBIOS) | UEFI (OVMF) |
|-------|----------------|-------------|
| Firmware loads boot entry | El Torito i386 → `bootxx_cd9660` ✓ | El Torito EFI (0xEF) → `BOOTX64.EFI` ✓ |
| Loader runs | NetBSD `boot` ✓ | GRUB 2.14 ✓ |
| Menu / config | `boot.cfg` ✓ | `grub.cfg` from ISO9660 ✓ |
| Kernel multiboot handoff | ✓ | ✓ (`HEAD64` → `KMAIN` reached) |
| SMP / ACPI bring-up | ✓ (CPU discovery, LAPIC, IOAPIC) | ✗ stops at `SMP_INIT-no_cpus` |
| Userland → `login:` | ✓ **boots to login** | ✗ no console output |

So BIOS boot is fully working (regression-clean after the linker change) and
the UEFI chain now boots the kernel into `kmain()` — previously impossible.

### Remaining work for a full UEFI boot to login

Two deeper kernel issues remain (both beyond the boot-media/loader layer, and
the point where the nnonaka reference also stopped):

1. **ACPI RSDP discovery under UEFI.**  The kernel finds the ACPI RSDP by
   scanning the legacy BIOS area (`0xE0000–0xFFFFF`).  Under UEFI the RSDP is
   published in the **EFI System Table configuration table** instead, so the
   scan fails and CPU/APIC enumeration finds nothing (`SMP_INIT-no_cpus`).
   The RSDP address must be obtained from firmware and handed to the kernel.
2. **Console under UEFI.**  MINIX writes to the VGA text buffer (`0xB8000`),
   which does not exist in UEFI graphics mode — hence GRUB's "no console will
   be available to OS".  A UEFI boot needs either a GOP/linear-framebuffer
   console in the kernel or a forced serial console.

The cleanest way to deliver both is the **multiboot2 upgrade path**, which
nnonaka used:

1. Add a second header in a `.multiboot2` section of `head.S` (magic
   `0xE85250D6`, architecture 0, with the required end tag and a framebuffer
   request tag), keeping the v1 header for BIOS.
2. Teach `pre_init` to detect the multiboot2 magic in EAX (`0x36D76289`) and
   parse the tag-based mb2 info for the memory map, the **ACPI RSDP tag**, and
   the **EFI framebuffer tag**.
3. Switch `multiboot`/`module` to `multiboot2`/`module2` in
   `releasetools/grub.cfg` (GRUB's `multiboot2` already requests these tags).

Until then, the hybrid ISO boots fully on BIOS and brings the kernel up to
`kmain()` on UEFI.
