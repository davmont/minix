#!/usr/bin/env python3
# $MINIX$
#
# mkgpt.py - build a GPT disk image with a hybrid MBR for a UEFI MINIX image.
#
# No host GPT tool (sgdisk/parted) is assumed and the in-tree nbpartition only
# writes MINIX MBR tables, so this builds a valid GPT by hand: primary and
# backup GPT headers (with the required CRC32s) and a partition-entry array.
#
# Two partitions are placed:
#   1. an EFI System Partition (FAT) -- the UEFI firmware boots GRUB from here;
#   2. a MINIX root partition (MFS) -- mounted on demand by the running kernel.
#
# UEFI firmware finds the ESP through the GPT.  MINIX, however, reads MBR
# partition tables rather than GPT, so we also write a *hybrid* MBR: a 0xEE
# protective entry (so the disk still presents as GPT) plus real MBR entries
# for the MINIX root and the ESP, so MINIX sees the root as /dev/c0d0p1.
#
# Usage: mkgpt.py <esp.img> <root.img> <out.img>

import sys, struct, zlib

SECTOR = 512
ALIGN = 2048                              # 1 MiB partition alignment
NUM_ENTRIES = 128
ENTRY_SIZE = 128
ENTRIES_LBAS = (NUM_ENTRIES * ENTRY_SIZE) // SECTOR    # 32

ESP_TYPE_GUID = bytes([                   # EFI System Partition
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B])
DATA_TYPE_GUID = bytes([                  # Linux filesystem data (generic)
    0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
    0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4])

DISK_GUID = bytes(range(0x10, 0x20))
ESP_PART_GUID = bytes(range(0x20, 0x30))
ROOT_PART_GUID = bytes(range(0x30, 0x40))

MBR_PROTECTIVE = 0xEE
MBR_MINIX = 0x81
MBR_EFI = 0xEF


def roundup(n, a):
    return (n + a - 1) // a * a


def utf16le_name(s):
    b = s.encode('utf-16-le')
    return b + b'\x00' * (72 - len(b))


def read_padded(path):
    d = open(path, 'rb').read()
    if len(d) % SECTOR:
        d += b'\x00' * (SECTOR - len(d) % SECTOR)
    return d, len(d) // SECTOR


def mbr_entry(boot, ptype, start, sectors):
    # CHS fields are left as "max" placeholders; firmware/MINIX use the LBA
    # start+size for large disks.
    start = min(start, 0xFFFFFFFF)
    sectors = min(sectors, 0xFFFFFFFF)
    return struct.pack('<B3sB3sII', boot, b'\x00\x02\x00', ptype,
                       b'\xff\xff\xff', start, sectors)


def main():
    if len(sys.argv) != 4:
        sys.stderr.write("usage: mkgpt.py <esp.img> <root.img> <out.img>\n")
        return 1
    esp_path, root_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]

    esp, esp_sec = read_padded(esp_path)
    root, root_sec = read_padded(root_path)

    esp_first = ALIGN
    esp_last = esp_first + esp_sec - 1
    root_first = roundup(esp_last + 1, ALIGN)
    root_last = root_first + root_sec - 1

    last_lba = root_last + 1 + ENTRIES_LBAS        # backup GPT header LBA
    total = last_lba + 1
    first_usable = 2 + ENTRIES_LBAS                # 34
    last_usable = root_last

    # --- GPT partition entry array ---
    def part(type_guid, part_guid, first, last, name):
        return struct.pack('<16s16sQQQ72s', type_guid, part_guid,
                           first, last, 0, utf16le_name(name))
    entries = (part(ESP_TYPE_GUID, ESP_PART_GUID, esp_first, esp_last,
                    "EFI System Partition") +
               part(DATA_TYPE_GUID, ROOT_PART_GUID, root_first, root_last,
                    "MINIX root"))
    entries += b'\x00' * (ENTRY_SIZE * NUM_ENTRIES - len(entries))
    entries_crc = zlib.crc32(entries) & 0xffffffff

    def gpt_header(my_lba, alt_lba, entries_lba):
        hdr = struct.pack('<8sIIIIQQQQ16sQIII',
                          b'EFI PART', 0x00010000, 92, 0, 0,
                          my_lba, alt_lba, first_usable, last_usable,
                          DISK_GUID, entries_lba, NUM_ENTRIES, ENTRY_SIZE,
                          entries_crc)
        crc = zlib.crc32(hdr) & 0xffffffff
        return (hdr[:16] + struct.pack('<I', crc) + hdr[20:]).ljust(SECTOR, b'\x00')

    primary_hdr = gpt_header(1, last_lba, 2)
    backup_hdr = gpt_header(last_lba, 1, root_last + 1)

    # --- hybrid MBR ---
    # MINIX sorts the MBR primaries by start LBA before numbering them
    # (libblockdriver/drvlib.c), so the partition device index depends on
    # start order, not slot order.  We therefore write exactly two entries:
    #   - a 0xEE protective entry over the GPT area *before* the ESP (LBA
    #     1..esp_first-1), so the disk still presents as GPT to firmware; and
    #   - the MINIX root.
    # Sorted by start LBA that is [protective, root], so MINIX sees the root
    # as /dev/c0d0p1.  The ESP is deliberately *not* in the MBR: UEFI finds it
    # via the GPT, and adding it here would sort ahead of the root and shift
    # the root to p2.
    mbr = bytearray(SECTOR)
    mbr[446:446+16]    = mbr_entry(0x00, MBR_PROTECTIVE, 1, esp_first - 1)
    mbr[446+16:446+32] = mbr_entry(0x00, MBR_MINIX, root_first, root_sec)
    mbr[510], mbr[511] = 0x55, 0xAA

    with open(out_path, 'wb') as f:
        f.truncate(total * SECTOR)
        f.seek(0);                        f.write(mbr)
        f.seek(1 * SECTOR);               f.write(primary_hdr)
        f.seek(2 * SECTOR);               f.write(entries)
        f.seek(esp_first * SECTOR);       f.write(esp)
        f.seek(root_first * SECTOR);      f.write(root)
        f.seek((root_last + 1) * SECTOR); f.write(entries)   # backup entries
        f.seek(last_lba * SECTOR);        f.write(backup_hdr)

    sys.stderr.write(
        "GPT %s: %d sectors; ESP %d..%d (p2), MINIX root %d..%d (p1)\n"
        % (out_path, total, esp_first, esp_last, root_first, root_last))
    return 0


if __name__ == '__main__':
    sys.exit(main())
