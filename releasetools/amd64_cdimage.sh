#!/usr/bin/env bash
set -e

#
# This script creates a bootable image and should at some point in the future
# be replaced by the proper NetBSD infrastructure.
#

: ${ARCH=amd64}
: ${OBJ=../obj.${ARCH}}
: ${TOOLCHAIN_TRIPLET=x86_64-elf64-minix-}
: ${BUILDSH=build.sh}

: ${SETS="minix-base minix-man"}
: ${IMG=minix_amd64.iso}
: ${BUNDLE_SETS=1}

# Opt-in: overlay the LXQt/Qt6 desktop onto the ISO.  Off by default because the
# desktop is an out-of-tree build (see releasetools/build-desktop.sh); a plain
# `release && amd64_cdimage.sh` is unchanged.  With MKDESKTOP=yes the desktop
# must already be installed into DESTDIR by build-desktop.sh.
: ${MKDESKTOP=no}

if [ ! -f ${BUILDSH} ]
then
	echo "Please invoke me from the root source dir, where ${BUILDSH} is."
	exit 1
fi

# set up disk creation environment
. releasetools/image.defaults
. releasetools/image.functions
. releasetools/efiboot.functions

# Build a hybrid BIOS+UEFI ISO when the GRUB EFI build tools are available.
# Falls back to a BIOS-only ISO (the historical behaviour) otherwise, so the
# build never hard-fails just because GRUB is missing on the build host.
: ${UEFI=auto}
case "${UEFI}" in
yes)	efi_check_tools || exit 1 ;;
auto)	efi_check_tools 2>/dev/null && UEFI=yes || UEFI=no ;;
esac

echo "Building work directory..."
build_workdir "$SETS"

echo "Adding extra files..."
workdir_add_cd_files

# Overlay the desktop (opt-in).  Must run before create_input_spec.
if [ "${MKDESKTOP}" = "yes" ]; then
	echo "Adding desktop (MKDESKTOP=yes)..."
	workdir_add_desktop
fi

# add kernel
workdir_add_kernel minix_default

# add boot.cfg
# The storage controller (AHCI/SATA vs legacy IDE) is auto-detected at boot
# by the ramdisk rc via /proc/pci, so a single entry works on both q35-class
# (AHCI) and i440fx-class (IDE) machines.  To force a choice when debugging,
# use "Edit menu option" / "Drop to boot prompt" and add ahci=yes or ahci=no.
cat >${ROOT_DIR}/boot.cfg <<END_BOOT_CFG
banner=Welcome to the MINIX 3 installation CD
banner================================================================================
banner=
menu=Regular MINIX 3:multiboot /boot/minix_default/kernel bootcd=1 cdproberoot=1 no_apic=0 acpi=1
menu=MINIX 3 (serial console, verbose):multiboot /boot/minix_default/kernel bootcd=1 cdproberoot=1 no_apic=0 acpi=1 console=tty00 verbose=3
menu=Edit menu option:edit
menu=Drop to boot prompt:prompt
clear=1
timeout=10
default=1
load=/boot/minix_default/mod01_ds
load=/boot/minix_default/mod02_rs
load=/boot/minix_default/mod03_pm
load=/boot/minix_default/mod04_sched
load=/boot/minix_default/mod05_vfs
load=/boot/minix_default/mod06_memory
load=/boot/minix_default/mod07_tty
load=/boot/minix_default/mod08_mib
load=/boot/minix_default/mod09_vm
load=/boot/minix_default/mod10_pfs
load=/boot/minix_default/mod11_mfs
load=/boot/minix_default/mod12_init
END_BOOT_CFG
add_file_spec "boot.cfg" extra.cdfiles

# set correct message of the day (log in and install tip)
cp releasetools/release/cd/etc/issue ${ROOT_DIR}/etc/issue
add_file_spec "etc/issue" extra.cdfiles

# /CD marker — etc/usr/rc tests `[ ! -f /CD ]` to skip cron and bind
# /var/log into /tmp on CD boots.  release.sh creates this for full
# release images; we need it here too so amd64_cdimage.sh ISOs behave
# the same way.  Without it the CD boot tries to start cron, which
# isn't useful from a read-only root anyway.
date > ${ROOT_DIR}/CD
add_file_spec "CD" extra.cdfiles

echo "Bundling packages..."
bundle_packages "$BUNDLE_PACKAGES"

# Place grub.cfg on the ISO-9660 volume for the UEFI boot path.  When booted
# via EFI El Torito the firmware does not expose the embedded ESP as a GRUB
# filesystem, so GRUB's baked-in early config locates the menu here instead
# (see releasetools/efiboot.functions).  This must happen before
# create_input_spec so the file is included in the ISO tree.
if [ "${UEFI}" = "yes" ]; then
	mkdir -p ${ROOT_DIR}/boot/grub
	cp releasetools/grub.cfg ${ROOT_DIR}/boot/grub/grub.cfg
	add_dir_spec "boot/grub" extra.cdfiles
	add_file_spec "boot/grub/grub.cfg" extra.cdfiles
fi

echo "Creating specification files..."
create_input_spec
create_protos

# Clean image
if [ -f ${IMG} ]	# IMG might be a block device
then
	rm -f ${IMG}
fi

# Assemble the EFI System Partition image (GRUB + grub.cfg) for the UEFI
# El Torito entry.  The on-ISO grub.cfg was already placed and registered in
# the input spec above (before create_input_spec); here we only build the FAT
# ESP image and append the second boot-catalog entry.
EFI_BOOTIMAGE=""
if [ "${UEFI}" = "yes" ]; then
	echo "Building EFI System Partition image..."
	ESP_IMG=$(efi_build_esp_image) || exit 1
	# Second El Torito entry: platform id 0xEF (EFI), no-emulation.  makefs
	# groups boot images by platform and emits the proper section headers;
	# this EFI El Torito support is nonaka's upstream NetBSD makefs work
	# (usr.sbin/makefs/cd9660/cd9660_eltorito.[ch]).
	EFI_BOOTIMAGE=",bootimage=efi;${ESP_IMG}"
fi

echo "Writing ISO..."
# The BIOS bootimage platform must be "i386" regardless of kernel arch:
# bootxx_cd9660 is the i386 BIOS El Torito loader and nbmakefs does not accept
# "amd64".  When UEFI is enabled a second "efi" bootimage is appended, yielding
# a hybrid catalog that boots on both legacy BIOS and UEFI firmware.
${CROSS_TOOLS}/nbmakefs -t cd9660 -F ${WORK_DIR}/input -o "rockridge,bootimage=i386;${DESTDIR}/usr/mdec/bootxx_cd9660${EFI_BOOTIMAGE},label=MINIX" ${IMG} ${ROOT_DIR}

echo ""
echo "ISO image at `pwd`/${IMG}"
echo ""
echo "To boot this image on kvm using the bootloader:"
echo "qemu-system-x86_64 --enable-kvm -cpu host -smp 4 -m 256 -cdrom `pwd`/${IMG}"
echo ""
echo "  NOTE: -cpu host is required — MINIX amd64 uses WRFSBASE for TLS and"
echo "  needs CR4.FSGSBASE, which QEMU's default kvm64 CPU does not expose."
echo ""
echo "To boot this image on kvm:"
echo "cd ${MODDIR} && qemu-system-x86_64 --enable-kvm -cpu host -kernel kernel -append \"bootcd=1 cdproberoot=1\" -initrd \"${mods}\" -cdrom `pwd`/${IMG}"
