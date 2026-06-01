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
| Kernel multiboot handoff | mb1 ✓ | mb2 ✓ (`HEAD64` → `KMAIN`) |
| SMP / ACPI bring-up | ✓ | ✓ (RSDP from mb2 ACPI tag) |
| Userland → `login:` | ✓ **boots to login** (VGA text) | ✓ **boots to login** (GOP framebuffer + serial) |
| Interactive shell on display | ✓ VGA text | ✓ framebuffer (keyboard echo, scroll, shell) |

Both paths boot to an interactive login.  Under UEFI the console renders on
the GOP framebuffer (see "Console on the physical screen" below); a serial
console is also available via the `console=tty00` menu entries.

## Multiboot2 (implemented)

Multiboot1 cannot carry the ACPI RSDP, and under UEFI the RSDP is not in the
legacy BIOS area the kernel scans (`0xE0000–0xFFFFF`) — it is published in the
EFI configuration table.  Without it, CPU/APIC enumeration finds nothing and
the kernel hangs after `SMP_INIT-no_cpus`.  The fix is Multiboot2, which hands
the kernel both the ACPI RSDP and a linear framebuffer:

1. `head.S` carries a `.multiboot2` OS-image header (magic `0xE85250D6`) next
   to the v1 header, with a framebuffer-request tag.  Both stay in the first
   page of the file (`-z max-page-size=0x1000`).
2. `pre_init.c` detects the mb2 boot magic (`0x36D76289`) and `mb2_to_mb1()`
   translates the tag-based block into the `multiboot_info_t` that
   `get_parameters()` already consumes (cmdline/modules/memory map), capturing
   the **ACPI RSDP** and the **EFI framebuffer** into `kinfo`.
3. `acpi.c` uses `kinfo.acpi_rsdp` directly when set, skipping the BIOS scan.
4. `releasetools/grub.cfg` loads the kernel with `multiboot2`/`module2`.

BIOS is unaffected: the NetBSD loader still uses the v1 header and the legacy
RSDP scan.  Verified: UEFI boots through full ACPI table enumeration → APIC
mode → multiuser → `login:`; BIOS regression-tested to login with the
identical SMP bring-up sequence.

## Console on the physical screen (implemented)

UEFI has no VGA text buffer (`0xB8000`), so console output needs the linear
framebuffer.  `pre_init` captures its geometry into
`kinfo.fb_addr/fb_pitch/fb_width/fb_height/fb_bpp` (32-bpp GOP), and the
**userland `tty` driver** (`minix/drivers/tty/tty/arch/i386/console.c`) renders
`/dev/console` into it:

- It reads the geometry via `get_minix_kerninfo()->kinfo` (a pointer into
  mapped kernel memory — no `kinfo` copy, so no `GET_KINFO` size-mismatch
  hazard) and maps the framebuffer with `vm_map_phys` in its own address space.
- `console_memory` becomes a RAM shadow of char+attr words; the three video
  primitives (`mem_vid_copy` / `vid_vid_copy` / `set_6845`) render the touched
  cells.  An 8x16 cell is drawn from a built-in public-domain 8x8 font (rows
  doubled); VGA attributes map to a 16-colour palette; the cursor is a software
  underline.  Virtual consoles share the framebuffer as they share VGA memory
  (each owns a shadow page, only the visible one is drawn, `select_console`
  repaints).
- When `kinfo.fb_addr == 0` (BIOS / Multiboot1) the legacy VGA text path is
  used unchanged.

Verified under QEMU+OVMF: full boot, `Welcome to MINIX`, login, keyboard echo,
scrolling and an interactive root shell all render on the UEFI GOP display;
BIOS keeps the VGA text console (regression-tested to a login prompt).

Note — a *kernel* framebuffer console was prototyped and rejected: the kernel's
`printf` goes to the `kmess` ring buffer, not `direct_print`, so it would only
render panics, not the login prompt; and mapping a multi-MB framebuffer through
the kernel's `arch_phys_map` mechanism (intended for small MMIO windows)
disturbs later mappings and breaks ACPI/PCI bring-up.  The console belongs in
`tty`, in userland.

### q35 (typical UEFI hardware) status

`-machine q35` (SATA/AHCI, no legacy IDE) — the usual UEFI shape — now boots
to login.  Four blockers were **fixed**:

1. **`at_wini` panic** (`no matching device found`) when there is no IDE
   controller — now prints a notice and exits with ENODEV so the CD probe can
   fall through to another driver.  (`minix/drivers/storage/at_wini`)
2. **`acpi` assert** in `pci.c` (`do_get_irq`/`add_irq`:
   `dev < PCI_MAX_DEVICES && pin < PCI_MAX_PINS`) on some UEFI/q35 PCI
   layouts — now returns/skips gracefully (the PCI server already falls back
   to `derive_irq`).  (`minix/drivers/power/acpi/pci.c`)
3. **AHCI not started on amd64** — the boot ramdisk rc ignored `ahci=yes` on
   amd64 and the AHCI driver was i386-only in the ramdisk.  The amd64 branch
   now honours `ahci=yes` and `/service/ahci` is included in the amd64 boot
   ramdisk.  (`minix/drivers/storage/ramdisk/{rc,Makefile,proto}`)

4. **AHCI spin-up timeout** — the AHCI port state machine only left
   `STATE_SPIN_UP` on a device-connect (PCS) interrupt.  QEMU's q35 ich9-ahci
   presents a *cold-plugged* device in `SSTS.DET` (DET=3) without ever setting
   PCS (no connection *change* for a device already attached at reset), so
   every port hit "spin-up timeout" and the SATA boot CD was never found.  On
   timeout the driver now also treats a device that is present per `SSTS.DET`
   as connected and polls it (extending the existing VirtualBox `IS.PCS`
   workaround).  (`minix/drivers/storage/ahci/ahci.c`)

With these, **q35 boots to a login prompt on the framebuffer console**: ACPI
enumerates (no assert), `at_wini` exits cleanly (no panic), the AHCI driver
detects the ATAPI CD on its SATA port (`ATAPI, QEMU DVD-ROM ... medium
detected`), `cdprobe` finds `/dev/c0d2`, the ISO-9660 root mounts, and
multiuser startup reaches `login:`.

Verified end to end: BIOS → VGA-text login; UEFI on `-machine pc` (IDE) →
framebuffer login; UEFI on `-machine q35` (SATA/AHCI) → framebuffer login.

## GPT USB / disk image (`amd64_gptimage.sh`)

`releasetools/amd64_gptimage.sh` builds a GPT-partitioned, UEFI-bootable disk
image (for a USB stick) in the spirit of nnonaka's GPT live image.  Since no
host GPT tool (sgdisk/parted) is assumed and the in-tree `nbpartition` only
writes MINIX MBR tables, `releasetools/mkgpt.py` constructs a valid GPT by
hand (protective MBR, primary/backup headers with CRC32s, one EFI System
Partition entry).  The single ESP (FAT, built with `nbmakefs -t msdos`) holds
the GRUB EFI loader, `grub.cfg`, the kernel and all boot modules; GRUB loads
the kernel via Multiboot2.

Verified under QEMU + OVMF: firmware reads the GPT, loads `BOOTX64.EFI` from
the ESP, GRUB hands off via multiboot2, and the kernel reaches `kmain()`.

**Root-filesystem limitation (amd64).** The script currently boots the
RAM-disk root model (`bootramdisk=1`): the whole root is embedded in
`mod06_memory` via `create_ramdisk_image`.  With the full `minix-base` set
that module is ~366 MB, and boot-image processes are **physically
pre-allocated** (`MF_PREALLOC` in `vm/main.c:boot_alloc`), so VM panics with
`exec: map_page_region for boot process failed` — a 366 MB boot process is too
large to pre-allocate at early boot.  (The amd64 RAM-disk path had never been
exercised before — `create_ramdisk_image` even passed an invalid objcopy
`-B x86_64`, now fixed to `i386:x86-64`.)  A usable GPT live image therefore
needs one of:

1. a **trimmed root set** small enough to pre-allocate as a boot process; or
2. a **real root partition** instead of a RAM disk — but MINIX reads MBR
   partition tables, not GPT, so this needs either a *hybrid MBR* (GPT for
   firmware + an MBR entry so MINIX finds the root partition) or GPT-partition
   support in the MINIX storage layer.

The GPT/ESP/GRUB/Multiboot2 boot chain itself is proven; the remaining work is
the root-filesystem strategy above.
