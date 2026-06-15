/* The dispatch table mapping fsdriver requests onto vfat handlers.
 *
 * vfat is currently read-only: write/create/modify hooks are omitted, so VFS
 * returns EROFS (or ENOSYS) for them automatically.
 */
#define _TABLE

#include "fs.h"

struct fsdriver vfat_table = {
	.fdr_mount	= fs_mount,
	.fdr_unmount	= fs_unmount,
	.fdr_lookup	= fs_lookup,
	.fdr_putnode	= fs_putnode,
	.fdr_read	= fs_readwrite,
	.fdr_write	= fs_readwrite,
	.fdr_trunc	= fs_trunc,
	.fdr_create	= fs_create,
	.fdr_mkdir	= fs_mkdir,
	.fdr_unlink	= fs_unlink,
	.fdr_rmdir	= fs_unlink,
	.fdr_rename	= fs_rename,
	.fdr_getdents	= fs_getdents,
	.fdr_stat	= fs_stat,
	.fdr_chmod	= fs_chmod,
	.fdr_utime	= fs_utime,
	.fdr_mountpt	= fs_mountpt,
	.fdr_statvfs	= fs_statvfs,
	.fdr_sync	= fs_sync,
	.fdr_driver	= lmfs_driver,
	.fdr_bread	= lmfs_bio,
	.fdr_bpeek	= lmfs_bio,
	.fdr_bflush	= lmfs_bflush
};
