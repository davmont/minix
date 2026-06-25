#ifndef _EXFAT_GLO_H_
#define _EXFAT_GLO_H_

#ifdef _TABLE
#undef EXTERN
#define EXTERN
#endif

#include <minix/fsdriver.h>

/* The mount state for the single file system this server instance serves. */
EXTERN struct exfat_mount exfat_mount;
EXTERN struct exfat_mount *pmp;		/* == &exfat_mount once mounted */

/* The in-core node table. */
EXTERN struct inode inode[NR_INODES];

/* Set TRUE once a file system is mounted. */
EXTERN int mounted;

/* The fsdriver dispatch table. */
extern struct fsdriver exfat_table;

#endif /* _EXFAT_GLO_H_ */
