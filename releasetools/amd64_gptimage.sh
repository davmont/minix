#!/usr/bin/env bash
# $MINIX$
set -e

#
# Build a GPT-partitioned, UEFI-bootable amd64 disk image (e.g. for a USB
# stick), in the spirit of Naoaki Nonaka's (nnonaka) GPT live-image work
# <https://github.com/nnonaka/minix>.
#
# Layout (see releasetools/mkgpt.py): a GPT with two partitions plus a hybrid
# MBR.
#   - EFI System Partition (FAT): GRUB EFI loader, grub.cfg, the kernel and
#     all boot modules.  UEFI firmware finds it via the GPT and runs GRUB,
#     which loads the kernel via Multiboot2.
#   - MINIX root partition (MFS): the full base system, mounted on demand by
#     the running kernel (rootdevname=c0d0p1).  MINIX reads MBR tables, not
#     GPT, so the hybrid MBR exposes this partition to MINIX as /dev/c0d0p1.
#
# This avoids embedding the whole root in a (physically pre-allocated) boot
# process, which does not scale on amd64.  See docs/UEFI_BOOT.md.
#
# Verified with QEMU + OVMF (edk2):
#   qemu-system-x86_64 --enable-kvm -m 1G -machine q35 \
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
# MINIX root partition device, as MINIX sees it through the hybrid MBR.
: ${ROOTDEV=c0d0p1}
# Root MFS size (bytes) and inode count.  Must exceed the base contents with
# headroom for /var, /tmp, /root etc., or the live system runs out of space
# and inodes at boot.  (4 KB MFS blocks.)
: ${ROOT_FS_SIZE=$(( 1536*1024*1024 ))}
: ${ROOT_FS_INODES=60000}

if [ ! -f ${BUILDSH} ]; then
	echo "Please invoke me from the root source dir, where ${BUILDSH} is."
	exit 1
fi

. releasetools/image.defaults
. releasetools/image.functions
. releasetools/efiboot.functions

MODDIR=${DESTDIR}/boot/minix/.temp

efi_check_tools || { echo "GRUB EFI build tools required (see efiboot.functions)"; exit 1; }

echo "Building work directory..."
build_workdir "$SETS"

# Single-root fstab: everything lives on the one MINIX partition; the boot
# ram-disk rc mounts it (rootdevname) and switches to it.
cat >${ROOT_DIR}/etc/fstab <<END_FSTAB
none		/sys		devman	rw,rslabel=devman	0	0
none		/dev/pts	ptyfs	rw,rslabel=ptyfs	0	0
END_FSTAB
add_file_spec "etc/fstab" extra.fstab

cp releasetools/release/cd/etc/issue ${ROOT_DIR}/etc/issue 2>/dev/null || true
add_file_spec "etc/issue" extra.cdfiles 2>/dev/null || true

echo "Bundling packages..."
bundle_packages "$BUNDLE_PACKAGES"

echo "Creating specification files..."
create_input_spec
create_protos

echo "Assembling EFI System Partition tree..."
ESP_ROOT=${WORK_DIR}/gptesp
rm -rf ${ESP_ROOT}
mkdir -p ${ESP_ROOT}/EFI/BOOT ${ESP_ROOT}/boot/minix_default

_efi_mkimage "x86_64-efi" "x86_64-efi" "${ESP_ROOT}/EFI/BOOT/BOOTX64.EFI"
_efi_mkimage "i386-efi"   "i386-efi"   "${ESP_ROOT}/EFI/BOOT/BOOTIA32.EFI"
[ -f "${ESP_ROOT}/EFI/BOOT/BOOTX64.EFI" ] || [ -f "${ESP_ROOT}/EFI/BOOT/BOOTIA32.EFI" ] || \
	{ echo "ERROR: no GRUB EFI binary produced"; exit 1; }

# kernel + boot modules (the normal, small mod06_memory probe ram disk: it
# brings up AHCI and mounts the real root partition).
cp ${MODDIR}/kernel ${ESP_ROOT}/boot/minix_default/kernel
cp ${MODDIR}/mod??_* ${ESP_ROOT}/boot/minix_default/

gen_modlines() {
	for m in $(cd ${ESP_ROOT}/boot/minix_default && echo mod??_*); do
		echo "    module2 /boot/minix_default/${m} ${m#mod??_}"
	done
}
cat > ${ESP_ROOT}/EFI/BOOT/grub.cfg <<END_GRUB_CFG
# $MINIX$  GPT/UEFI disk image: boot the MINIX root partition (${ROOTDEV}).
set timeout=5
set default=0
search --no-floppy --file --set=root /boot/minix_default/kernel

menuentry "MINIX 3 (UEFI, GPT)" {
    multiboot2 /boot/minix_default/kernel rootdevname=${ROOTDEV} ahci=yes no_apic=0 acpi=1
$(gen_modlines)
}

menuentry "MINIX 3 (UEFI, GPT, serial console)" {
    multiboot2 /boot/minix_default/kernel rootdevname=${ROOTDEV} ahci=yes no_apic=0 acpi=1 console=tty00
$(gen_modlines)
}
END_GRUB_CFG

echo "Creating EFI System Partition (FAT) image..."
ESP_BYTES=$(du -sk ${ESP_ROOT} | awk '{print $1*1024}')
ESP_BYTES=$(( ESP_BYTES + ESP_BYTES/4 ))
MIN_BYTES=$(( 48*1024*1024 ))
[ ${ESP_BYTES} -lt ${MIN_BYTES} ] && ESP_BYTES=${MIN_BYTES}
ESP_BYTES=$(( (ESP_BYTES + 511) / 512 * 512 ))
# FAT16: an ESP holding ~10 MB of loader+kernel+modules is well under the
# FAT32 minimum cluster count; UEFI accepts FAT12/16/32 on removable media.
${CROSS_TOOLS}/nbmakefs -t msdos -s ${ESP_BYTES} \
	-o "fat_type=16,volume_label=MINIXEFI" \
	${WORK_DIR}/gptesp.img ${ESP_ROOT}

echo "Creating MINIX root (MFS) partition image..."
${CROSS_TOOLS}/nbmkfs.mfs -b $(( ROOT_FS_SIZE / 4096 )) -i ${ROOT_FS_INODES} \
	-I 0 ${WORK_DIR}/gptroot.img ${WORK_DIR}/proto.root

echo "Wrapping ESP + root in a GPT disk image (hybrid MBR)..."
rm -f ${IMG}
python3 releasetools/mkgpt.py ${WORK_DIR}/gptesp.img ${WORK_DIR}/gptroot.img ${IMG}

echo ""
echo "GPT UEFI disk image at `pwd`/${IMG}"
echo ""
echo "Boot under QEMU + OVMF:"
echo "  qemu-system-x86_64 --enable-kvm -m 1G -machine q35 \\"
echo "    -drive if=pflash,format=raw,readonly=on,file=OVMF_CODE.fd \\"
echo "    -drive if=pflash,format=raw,file=OVMF_VARS.fd \\"
echo "    -drive format=raw,file=`pwd`/${IMG}"
