/*	$NetBSD: bootsect.h,v 1.5 2012/11/04 17:57:59 jakllsch Exp $	*/

/*
 * Written by Paul Popelka (paulp@uts.amdahl.com)
 *
 * You can do anything you want with this software, just don't say you wrote
 * it, and don't remove this notice.
 *
 * This software is provided "as is".
 *
 * Imported into MINIX from NetBSD sys/fs/msdosfs for the vfat server.
 */
#ifndef _VFAT_BOOTSECT_H_
#define _VFAT_BOOTSECT_H_

/*
 * Format of a boot sector.  This is the first sector on a DOS floppy disk
 * or the first sector of a partition on a hard disk.
 */
struct bootsector33 {
	uint8_t		bsJump[3];		/* jump inst E9xxxx or EBxx90 */
	int8_t		bsOemName[8];		/* OEM name and version */
	int8_t		bsBPB[19];		/* BIOS parameter block */
	int8_t		bsDriveNumber;		/* drive number (0x80) */
	int8_t		bsBootCode[479];	/* pad so struct is 512b */
	uint8_t		bsBootSectSig0;
	uint8_t		bsBootSectSig1;
#define	BOOTSIG0	0x55
#define	BOOTSIG1	0xaa
};

struct bootsector50 {
	uint8_t		bsJump[3];		/* jump inst E9xxxx or EBxx90 */
	int8_t		bsOemName[8];		/* OEM name and version */
	int8_t		bsBPB[25];		/* BIOS parameter block */
	int8_t		bsExt[26];		/* Bootsector Extension */
	int8_t		bsBootCode[448];	/* pad so structure is 512b */
	uint8_t		bsBootSectSig0;
	uint8_t		bsBootSectSig1;
};

struct bootsector710 {
	uint8_t		bsJump[3];		/* jump inst E9xxxx or EBxx90 */
	int8_t		bsOEMName[8];		/* OEM name and version */
	int8_t		bsBPB[53];		/* BIOS parameter block */
	int8_t		bsExt[26];		/* Bootsector Extension */
	int8_t		bsBootCode[420];	/* pad so structure is 512b */
	uint8_t		bsBootSectSig0;
	uint8_t		bsBootSectSig1;
};

union bootsector {
	struct bootsector33 bs33;
	struct bootsector50 bs50;
	struct bootsector710 bs710;
};

#endif /* _VFAT_BOOTSECT_H_ */
