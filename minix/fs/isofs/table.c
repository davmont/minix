
/*
 * This file contains the table used to map system call numbers onto the
 * routines that perform them.
 */

#define _TABLE

#include "inc.h"

struct fsdriver isofs_table = {
	.fdr_mount	= fs_mount,
	.fdr_unmount	= fs_unmount,
	.fdr_lookup	= fs_lookup,
	.fdr_putnode	= fs_putnode,
	.fdr_read	= fs_read,
	.fdr_peek	= fs_peek,	/* assembles full pages despite 2K blocks */
	.fdr_getdents	= fs_getdents,
	.fdr_rdlink	= fs_rdlink,
	.fdr_stat	= fs_stat,
	.fdr_mountpt	= fs_mountpt,
	.fdr_statvfs	= fs_statvfs,
	.fdr_driver	= lmfs_driver,
	.fdr_bread	= lmfs_bio,
	.fdr_bwrite	= lmfs_bio,
	.fdr_bpeek	= lmfs_bio,	/* device-level peek (block-special) */
	.fdr_bflush	= lmfs_bflush
};
