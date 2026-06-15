#ifndef _VFAT_GLO_H_
#define _VFAT_GLO_H_

#ifdef _TABLE
#undef EXTERN
#define EXTERN
#endif

#include <minix/fsdriver.h>

/* The mount state for the single file system this server instance serves. */
EXTERN struct fat_mount fat_mount;
EXTERN struct fat_mount *pmp;		/* == &fat_mount once mounted */

/* The in-core node table. */
EXTERN struct inode inode[NR_INODES];

/* Set TRUE once a file system is mounted. */
EXTERN int mounted;

/* The fsdriver dispatch table. */
extern struct fsdriver vfat_table;

#endif /* _VFAT_GLO_H_ */
