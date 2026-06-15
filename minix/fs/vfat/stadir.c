/* stat / statvfs for the vfat server. */
#include "fs.h"
#include <sys/stat.h>
#include <sys/statvfs.h>

/*===========================================================================*
 *				fs_stat					     *
 *===========================================================================*/
int fs_stat(ino_t ino_nr, struct stat *statbuf)
{
	struct inode *rip;

	if ((rip = find_inode(ino_nr)) == NULL)
		return EINVAL;

	memset(statbuf, 0, sizeof(*statbuf));

	statbuf->st_dev = rip->i_dev;
	statbuf->st_ino = rip->i_num;
	statbuf->st_mode = rip->i_mode;
	statbuf->st_nlink = (rip->i_attrs & ATTR_DIRECTORY) ? 2 : 1;
	statbuf->st_uid = pmp->pm_uid;
	statbuf->st_gid = pmp->pm_gid;
	statbuf->st_rdev = NO_DEV;
	statbuf->st_size = rip->i_size;
	statbuf->st_atime = rip->i_mtime;
	statbuf->st_mtime = rip->i_mtime;
	statbuf->st_ctime = rip->i_mtime;
	statbuf->st_blksize = pmp->pm_bpcluster;
	statbuf->st_blocks = (rip->i_size + 511) / 512;

	return OK;
}

/*===========================================================================*
 *				fs_statvfs				     *
 *===========================================================================*/
int fs_statvfs(struct statvfs *st)
{
	st->f_flag = ST_NOTRUNC;
	st->f_bsize = pmp->pm_bpcluster;
	st->f_frsize = pmp->pm_bpcluster;
	st->f_iosize = pmp->pm_bpcluster;
	st->f_blocks = pmp->pm_nmbrofclusters;
	st->f_bfree = 0;		/* free-cluster count not tracked (RO) */
	st->f_bavail = 0;
	st->f_files = 0;
	st->f_ffree = 0;
	st->f_favail = 0;
	st->f_namemax = WIN_MAXLEN;

	return OK;
}
