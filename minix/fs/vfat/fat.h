/*	$NetBSD: fat.h,v 1.9 2014/10/18 08:33:28 snj Exp $	*/

/*-
 * Copyright (C) 1994, 1997 Wolfgang Solfrank.
 * Copyright (C) 1994, 1997 TooLs GmbH.
 * All rights reserved.
 * Original code by Paul Popelka (paulp@uts.amdahl.com).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the conditions in the NetBSD
 * license are met.  THIS SOFTWARE IS PROVIDED ``AS IS''.
 *
 * Imported into MINIX from NetBSD sys/fs/msdosfs for the vfat server.
 */
#ifndef _VFAT_FAT_H_
#define _VFAT_FAT_H_

/* Some useful cluster numbers. */
#define	MSDOSFSROOT	0		/* cluster 0 means the root dir */
#define	CLUST_FREE	0		/* cluster 0 also means a free cluster */
#define	MSDOSFSFREE	CLUST_FREE
#define	CLUST_FIRST	2		/* first legal cluster number */
#define	CLUST_RSRVD	0xfffffff6	/* reserved cluster range */
#define	CLUST_BAD	0xfffffff7	/* a cluster with a defect */
#define	CLUST_EOFS	0xfffffff8	/* start of eof cluster range */
#define	CLUST_EOFE	0xffffffff	/* end of eof cluster range */
#define	CLUST_END	CLUST_EOFE	/* bigger than any valid cluster */

#define	FAT12_MASK	0x00000fff	/* mask for 12 bit cluster numbers */
#define	FAT16_MASK	0x0000ffff	/* mask for 16 bit cluster numbers */
#define	FAT32_MASK	0x0fffffff	/* mask for FAT32 cluster numbers */

#define	FAT12(pmp)	(pmp->pm_fatmask == FAT12_MASK)
#define	FAT16(pmp)	(pmp->pm_fatmask == FAT16_MASK)
#define	FAT32(pmp)	(pmp->pm_fatmask == FAT32_MASK)

/*
 * EOF mark is anything between 0xfffffff8 and 0xffffffff (masked by the
 * appropriate fatmask).  cn is supposed to be already adjusted to FAT type.
 */
#define	MSDOSFSEOF(cn, fatmask)	\
	(((cn) & CLUST_EOFS) == (CLUST_EOFS & (fatmask)))

#endif /* _VFAT_FAT_H_ */
