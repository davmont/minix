/* On-disk structures and constants for the exFAT file system.
 *
 * Layout and field offsets follow the Microsoft exFAT file system
 * specification (revision 1.00).  All multi-byte fields are little-endian and
 * are declared as byte arrays here; the le*dec()/le*enc() helpers in
 * <sys/endian.h> are used to read and write them, so the structures need no
 * particular host alignment.  Written for MINIX from the published spec (not
 * ported from any GPL/BSD source), so the whole format is self-contained.
 */
#ifndef _EXFAT_EXFAT_H_
#define _EXFAT_EXFAT_H_

#include <sys/endian.h>

/* Little-endian field accessors (the exFAT structures are all byte arrays). */
#define ex_get16(p)	le16dec(p)
#define ex_get32(p)	le32dec(p)
#define ex_get64(p)	le64dec(p)
#define ex_put16(p, v)	le16enc((p), (uint16_t)(v))
#define ex_put32(p, v)	le32enc((p), (uint32_t)(v))
#define ex_put64(p, v)	le64enc((p), (uint64_t)(v))

/*
 * Main Boot Sector (logical sector 0).  Only the fixed header through the boot
 * signature is described; for sector sizes larger than 512 the trailing bytes
 * up to the sector size are excess BootCode.  The boot signature 0xAA55 always
 * sits at byte offset 510.
 */
struct exfat_boot {
	uint8_t	jump_boot[3];		/* 0xEB 0x76 0x90 */
	uint8_t	fs_name[8];		/* "EXFAT   " */
	uint8_t	must_be_zero[53];
	uint8_t	partition_offset[8];	/* in sectors */
	uint8_t	volume_length[8];	/* in sectors */
	uint8_t	fat_offset[4];		/* in sectors */
	uint8_t	fat_length[4];		/* in sectors, per FAT */
	uint8_t	cluster_heap_offset[4];	/* in sectors */
	uint8_t	cluster_count[4];	/* number of clusters in the heap */
	uint8_t	first_cluster_of_root[4];
	uint8_t	volume_serial[4];
	uint8_t	fs_revision[2];		/* 0x0100 for revision 1.00 */
	uint8_t	volume_flags[2];
	uint8_t	bytes_per_sector_shift;	/* log2(bytes/sector), 9..12 */
	uint8_t	sectors_per_cluster_shift; /* log2(sectors/cluster), 0..25-x */
	uint8_t	number_of_fats;		/* 1, or 2 only for TexFAT */
	uint8_t	drive_select;
	uint8_t	percent_in_use;
	uint8_t	reserved[7];
	uint8_t	boot_code[390];
	uint8_t	boot_signature[2];	/* 0x55 0xAA */
};

#define EXFAT_FS_NAME		"EXFAT   "	/* 8 bytes, space padded */
#define EXFAT_BOOT_SIG		0xAA55

/* VolumeFlags bits. */
#define EXFAT_FLAG_ACTIVE_FAT	0x0001	/* which FAT is active (TexFAT) */
#define EXFAT_FLAG_VOLUME_DIRTY	0x0002
#define EXFAT_FLAG_MEDIA_FAILURE 0x0004

/* FAT entry values (32-bit entries). */
#define EXFAT_CLUST_FREE	0x00000000
#define EXFAT_CLUST_FIRST	0x00000002	/* first valid data cluster */
#define EXFAT_CLUST_BAD		0xFFFFFFF7
#define EXFAT_CLUST_EOF		0xFFFFFFFF	/* end of chain */

/* A 32-byte generic directory entry. */
struct exfat_dentry {
	uint8_t	type;			/* EntryType */
	uint8_t	data[31];
};

#define EXFAT_DENTRY_SIZE	32

/*
 * EntryType byte.  Bit 7 (InUse) clear means the entry (and the rest of its
 * set) is deleted/free; type 0x00 marks the end of the directory.  The low 5
 * bits are the type code and bit 6 (Category) selects primary vs secondary.
 */
#define EXFAT_TYPE_END		0x00	/* end-of-directory marker */
#define EXFAT_TYPE_INUSE	0x80	/* InUse bit */

#define EXFAT_ENTRY_BITMAP	0x81	/* Allocation Bitmap (root) */
#define EXFAT_ENTRY_UPCASE	0x82	/* Up-case Table (root) */
#define EXFAT_ENTRY_LABEL	0x83	/* Volume Label (root) */
#define EXFAT_ENTRY_FILE	0x85	/* File / directory */
#define EXFAT_ENTRY_STREAM	0xC0	/* Stream Extension (secondary) */
#define EXFAT_ENTRY_NAME	0xC1	/* File Name (secondary) */

/* Allocation Bitmap directory entry (type 0x81). */
struct exfat_bitmap_entry {
	uint8_t	type;
	uint8_t	flags;			/* bit0: 0 = first bitmap */
	uint8_t	reserved[18];
	uint8_t	first_cluster[4];
	uint8_t	data_length[8];		/* bytes */
};

/* Up-case Table directory entry (type 0x82). */
struct exfat_upcase_entry {
	uint8_t	type;
	uint8_t	reserved1[3];
	uint8_t	checksum[4];
	uint8_t	reserved2[12];
	uint8_t	first_cluster[4];
	uint8_t	data_length[8];		/* bytes */
};

/* Volume Label directory entry (type 0x83). */
struct exfat_label_entry {
	uint8_t	type;
	uint8_t	char_count;		/* number of UTF-16 chars (<= 11) */
	uint8_t	label[22];		/* UTF-16LE */
	uint8_t	reserved[8];
};

/* File directory entry (type 0x85), the primary entry of a file/dir set. */
struct exfat_file_entry {
	uint8_t	type;
	uint8_t	secondary_count;	/* # of following secondary entries */
	uint8_t	set_checksum[2];	/* checksum over the whole entry set */
	uint8_t	attributes[2];		/* FAT-style attribute bits */
	uint8_t	reserved1[2];
	uint8_t	create_time[4];		/* exFAT timestamp */
	uint8_t	modify_time[4];
	uint8_t	access_time[4];
	uint8_t	create_10ms;		/* 0..199 (×10 ms) */
	uint8_t	modify_10ms;
	uint8_t	create_utc_offset;	/* signed ×15 min, bit7 = valid */
	uint8_t	modify_utc_offset;
	uint8_t	access_utc_offset;
	uint8_t	reserved2[7];
};

/* Stream Extension directory entry (type 0xC0), first secondary entry. */
struct exfat_stream_entry {
	uint8_t	type;
	uint8_t	flags;			/* GeneralSecondaryFlags */
	uint8_t	reserved1;
	uint8_t	name_length;		/* length of the name in UTF-16 chars */
	uint8_t	name_hash[2];		/* hash of the up-cased name */
	uint8_t	reserved2[2];
	uint8_t	valid_data_length[8];	/* bytes actually written */
	uint8_t	reserved3[4];
	uint8_t	first_cluster[4];
	uint8_t	data_length[8];		/* allocated size in bytes */
};

/* File Name directory entry (type 0xC1), one or more secondary entries. */
struct exfat_name_entry {
	uint8_t	type;
	uint8_t	flags;
	uint8_t	name[30];		/* 15 UTF-16LE code units */
};

#define EXFAT_NAME_CHARS_PER_ENTRY	15
#define EXFAT_NAME_MAX			255	/* UTF-16 code units */

/* GeneralSecondaryFlags (stream entry). */
#define EXFAT_SECFLAG_ALLOC_POSSIBLE	0x01
#define EXFAT_SECFLAG_NO_FAT_CHAIN	0x02

/* File attribute bits (identical to FAT). */
#define EXFAT_ATTR_READONLY	0x0001
#define EXFAT_ATTR_HIDDEN	0x0002
#define EXFAT_ATTR_SYSTEM	0x0004
#define EXFAT_ATTR_DIRECTORY	0x0010
#define EXFAT_ATTR_ARCHIVE	0x0020

#endif /* _EXFAT_EXFAT_H_ */
