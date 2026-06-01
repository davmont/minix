#!/usr/bin/env python3
# $MINIX$
#
# mkgpt.py - wrap a single FAT EFI System Partition image in a GPT disk image.
#
# No host GPT tool (sgdisk/parted) is assumed to be available, and the in-tree
# nbpartition only writes MINIX MBR tables, so this builds a minimal but valid
# GPT by hand: a protective MBR, primary and backup GPT headers (with the
# required CRC32s), and a single partition entry of type "EFI System
# Partition".  The ESP is placed at a 1 MiB boundary, as firmware expects.
#
# Usage: mkgpt.py <esp.img> <out.img>
#
# Layout (512-byte LBAs):
#   0            protective MBR
#   1            primary GPT header
#   2 .. 33      primary partition entry array (128 * 128 B = 32 LBAs)
#   2048 .. N    EFI System Partition (the FAT image)
#   last-33..-2  backup partition entry array
#   last         backup GPT header

import sys, struct, zlib, os

SECTOR = 512
ESP_FIRST_LBA = 2048                     # 1 MiB alignment
NUM_ENTRIES = 128
ENTRY_SIZE = 128
ENTRIES_LBAS = (NUM_ENTRIES * ENTRY_SIZE) // SECTOR   # 32

# Type GUID for an EFI System Partition (C12A7328-F81F-11D2-BA4B-00A0C93EC93B).
ESP_TYPE_GUID = bytes([
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B])

# A couple of fixed GUIDs.  Reproducibility matters more than uniqueness for a
# build artifact, so these are constant rather than random.
DISK_GUID = bytes(range(0x10, 0x20))
PART_GUID = bytes(range(0x20, 0x30))


def utf16le_name(s):
    b = s.encode('utf-16-le')
    return b + b'\x00' * (72 - len(b))


def main():
    if len(sys.argv) != 3:
        sys.stderr.write("usage: mkgpt.py <esp.img> <out.img>\n")
        return 1
    esp_path, out_path = sys.argv[1], sys.argv[2]

    esp = open(esp_path, 'rb').read()
    if len(esp) % SECTOR:
        esp += b'\x00' * (SECTOR - len(esp) % SECTOR)
    esp_sectors = len(esp) // SECTOR

    esp_first = ESP_FIRST_LBA
    esp_last = esp_first + esp_sectors - 1            # inclusive
    # backup entry array + header live in the last 33 LBAs.
    last_lba = esp_last + 1 + ENTRIES_LBAS            # index of backup header
    total_sectors = last_lba + 1

    # --- partition entry array ---
    entry = struct.pack('<16s16sQQQ72s',
                        ESP_TYPE_GUID, PART_GUID,
                        esp_first, esp_last, 0,
                        utf16le_name("EFI System Partition"))
    entries = entry + b'\x00' * (ENTRY_SIZE * (NUM_ENTRIES - 1))
    entries_crc = zlib.crc32(entries) & 0xffffffff

    def gpt_header(my_lba, alt_lba, entries_lba):
        # header_crc32 field is zeroed for the CRC computation, then patched.
        hdr = struct.pack('<8sIIIIQQQQ16sQIII',
                          b'EFI PART', 0x00010000, 92, 0, 0,
                          my_lba, alt_lba,
                          esp_first, esp_last,      # first/last usable (ESP only)
                          DISK_GUID,
                          entries_lba, NUM_ENTRIES, ENTRY_SIZE, entries_crc)
        crc = zlib.crc32(hdr) & 0xffffffff
        return hdr[:16] + struct.pack('<I', crc) + hdr[20:]

    primary_hdr = gpt_header(1, last_lba, 2)
    backup_hdr = gpt_header(last_lba, 1, esp_last + 1)

    # --- protective MBR ---
    mbr = bytearray(SECTOR)
    # single 0xEE partition spanning the disk (capped at 0xFFFFFFFF sectors).
    part = struct.pack('<B3sB3sII', 0x00, b'\x00\x00\x02', 0xEE,
                       b'\xff\xff\xff',
                       1, min(total_sectors - 1, 0xFFFFFFFF))
    mbr[446:446 + 16] = part
    mbr[510] = 0x55
    mbr[511] = 0xAA

    # --- assemble the image ---
    with open(out_path, 'wb') as f:
        f.truncate(total_sectors * SECTOR)
        f.seek(0);                 f.write(mbr)
        f.seek(1 * SECTOR);        f.write(primary_hdr.ljust(SECTOR, b'\x00'))
        f.seek(2 * SECTOR);        f.write(entries)
        f.seek(esp_first * SECTOR); f.write(esp)
        f.seek((esp_last + 1) * SECTOR); f.write(entries)   # backup entries
        f.seek(last_lba * SECTOR); f.write(backup_hdr.ljust(SECTOR, b'\x00'))

    sys.stderr.write("GPT image %s: %d sectors, ESP %d sectors at LBA %d\n" %
                     (out_path, total_sectors, esp_sectors, esp_first))
    return 0


if __name__ == '__main__':
    sys.exit(main())
