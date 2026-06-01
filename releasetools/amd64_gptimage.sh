#!/usr/bin/env bash
# $MINIX$
set -e

#
# Build a GPT-partitioned, UEFI-bootable amd64 live image (e.g. for a USB
# stick), in the spirit of Naoaki Nonaka's (nnonaka) GPT live-image work
# <https://github.com/nnonaka/minix>.
#
# The image has a single GPT partition: an EFI System Partition (FAT) that
# holds the GRUB EFI loader, its grub.cfg, the MINIX kernel and all boot
# modules.  GRUB loads the kernel via Multiboot2 and the whole userland comes
# up from the RAM-disk root embedded in mod06_memory (bootramdisk=1) -- so the
# firmware/loader only ever needs to read FAT, and MINIX never has to parse a
# GPT data partition.  See docs/UEFI_BOOT.md.
#
# Verified with QEMU + OVMF (edk2):
#   qemu-system-x86_64 --enable-kvm -m 1G \
#       -drive if=pflash,format=raw,readonly=on,file=OVMF_CODE.fd \
#       -drive if=pflash,format=raw,file=OVMF_VARS.fd \
#       -drive format=raw,file=minix_amd64_gpt.img
#

: ${ARCH=amd64}
: ${OBJ=../obj.${ARCH}}
: ${TOOLCHAIN_TRIPLET=x86_64-elf64-minix-}
: ${BUILDSH=build.sh}

: ${SETS="minix-base"}
: ${IMG=minix_amd64_gpt.img}

if [ ! -f ${BUILDSH} ]; then
	echo "Please invoke me from the root source dir, where ${BUILDSH} is."
	exit 1
fi

# set up disk creation environment
. releasetools/image.defaults
. releasetools/image.functions
. releasetools/efiboot.functions

# where the freshly built kernel & boot modules live
MODDIR=${DESTDIR}/boot/minix/.temp

efi_check_tools || { echo "GRUB EFI build tools required (see efiboot.functions)"; exit 1; }

echo "Building work directory..."
build_workdir "$SETS"

echo "Adding ramdisk files..."
workdir_add_ramdisk_files

# log-in / install tip on the live console
cp releasetools/release/ramdisk/etc/issue ${ROOT_DIR}/etc/issue 2>/dev/null || true
add_file_spec "etc/issue" extra.cdfiles 2>/dev/null || true

echo "Bundling packages..."
bundle_packages "$BUNDLE_PACKAGES"

echo "Creating specification files..."
create_input_spec
create_protos

echo "Building RAM-disk root (embedded in mod06_memory)..."
# Stage the freshly built modules, then overwrite mod06_memory with one that
# embeds the full root filesystem (proto.root) as its RAM disk.
cp ${MODDIR}/* ${WORK_DIR}
create_ramdisk_image ${RAMDISK_SIZE}

echo "Assembling EFI System Partition tree..."
ESP_ROOT=${WORK_DIR}/gptesp
rm -rf ${ESP_ROOT}
mkdir -p ${ESP_ROOT}/EFI/BOOT ${ESP_ROOT}/boot/minix_default

# GRUB EFI loaders (BOOTX64.EFI / BOOTIA32.EFI) -- reuse the helper that also
# bakes in the early config that locates grub.cfg.
_efi_mkimage "x86_64-efi" "x86_64-efi" "${ESP_ROOT}/EFI/BOOT/BOOTX64.EFI"
_efi_mkimage "i386-efi"   "i386-efi"   "${ESP_ROOT}/EFI/BOOT/BOOTIA32.EFI"
[ -f "${ESP_ROOT}/EFI/BOOT/BOOTX64.EFI" ] || [ -f "${ESP_ROOT}/EFI/BOOT/BOOTIA32.EFI" ] || \
	{ echo "ERROR: no GRUB EFI binary produced"; exit 1; }

# Kernel + modules (mod06_memory is the RAM-disk-bearing one from WORK_DIR).
cp ${WORK_DIR}/kernel ${ESP_ROOT}/boot/minix_default/kernel
cp ${WORK_DIR}/mod??_* ${ESP_ROOT}/boot/minix_default/

# grub.cfg for the GPT live image: boot the RAM-disk root via Multiboot2.
mods=""
gen_modlines() {
	for m in $(cd ${ESP_ROOT}/boot/minix_default && echo mod??_*); do
		echo "    module2 /boot/minix_default/${m} ${m#mod??_}"
	done
}
cat > ${ESP_ROOT}/EFI/BOOT/grub.cfg <<END_GRUB_CFG
# $MINIX$  GPT/UEFI live image (RAM-disk root).
set timeout=5
set default=0
search --no-floppy --file --set=root /boot/minix_default/kernel

menuentry "MINIX 3 (UEFI live)" {
    multiboot2 /boot/minix_default/kernel bootramdisk=1 no_apic=0 acpi=1
$(gen_modlines)
}

menuentry "MINIX 3 (UEFI live, serial console)" {
    multiboot2 /boot/minix_default/kernel bootramdisk=1 no_apic=0 acpi=1 console=tty00
$(gen_modlines)
}
END_GRUB_CFG

echo "Creating EFI System Partition (FAT) image..."
# Size the FAT to the payload plus 25% slack, min 64 MiB (FAT32 floor),
# rounded up to a whole sector.
ESP_BYTES=$(du -sk ${ESP_ROOT} | awk '{print $1*1024}')
ESP_BYTES=$(( ESP_BYTES + ESP_BYTES/4 ))
MIN_BYTES=$(( 64*1024*1024 ))
[ ${ESP_BYTES} -lt ${MIN_BYTES} ] && ESP_BYTES=${MIN_BYTES}
ESP_BYTES=$(( (ESP_BYTES + 511) / 512 * 512 ))
${CROSS_TOOLS}/nbmakefs -t msdos -s ${ESP_BYTES} \
	-o "fat_type=32,volume_label=MINIXEFI" \
	${WORK_DIR}/gptesp.img ${ESP_ROOT}

echo "Wrapping ESP in a GPT disk image..."
rm -f ${IMG}
python3 releasetools/mkgpt.py ${WORK_DIR}/gptesp.img ${IMG}

echo ""
echo "GPT UEFI live image at `pwd`/${IMG}"
echo ""
echo "Boot under QEMU + OVMF:"
echo "  qemu-system-x86_64 --enable-kvm -m 1G \\"
echo "    -drive if=pflash,format=raw,readonly=on,file=OVMF_CODE.fd \\"
echo "    -drive if=pflash,format=raw,file=OVMF_VARS.fd \\"
echo "    -drive format=raw,file=`pwd`/${IMG}"
