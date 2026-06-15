/* Write support for the vfat server: directory-entry writeback, file extend
 * and truncate, and sync.
 */
#include "fs.h"
#include <sys/stat.h>

/*===========================================================================*
 *				vfat_now				     *
 *===========================================================================*/
time_t vfat_now(void)
{
	return clock_time(NULL);
}

/*===========================================================================*
 *				update_direntry				     *
 *===========================================================================*/
int update_direntry(struct inode *rip)
{
/* Write back the mutable fields (start cluster, size, modification time) of an
 * inode into its on-disk directory entry.  The root directory has no entry.
 */
	struct buf *bp;
	struct direntry *dep;
	unsigned long sec, secoff;
	uint16_t dosdate, dostime;

	if (rip->i_root)
		return OK;

	sec = entry_sector(rip->i_dirclust, rip->i_diroffset);
	if (sec == 0)
		return EIO;

	if ((bp = get_block(pmp->pm_dev, sec, NORMAL)) == NULL)
		return EIO;

	secoff = rip->i_diroffset % pmp->pm_BytesPerSec;
	dep = (struct direntry *) (b_data(bp) + secoff);

	putushort(dep->deStartCluster, rip->i_start & 0xffff);
	if (FAT32(pmp))
		putushort(dep->deHighClust, (rip->i_start >> 16) & 0xffff);
	if (!(rip->i_attrs & ATTR_DIRECTORY))
		putulong(dep->deFileSize, rip->i_size);

	unix2dostime(rip->i_mtime, &dosdate, &dostime);
	putushort(dep->deMDate, dosdate);
	putushort(dep->deMTime, dostime);
	dep->deAttributes = rip->i_attrs;

	lmfs_markdirty(bp);
	put_block(bp);

	return OK;
}

/*===========================================================================*
 *				zero_cluster				     *
 *===========================================================================*/
int zero_cluster(unsigned long cn)
{
/* Zero every sector of data cluster cn (used when growing files/dirs). */
	unsigned long bn, s;
	struct buf *bp;

	bn = cntobn(pmp, cn);
	for (s = 0; s < pmp->pm_bpcluster / pmp->pm_BytesPerSec; s++) {
		if ((bp = get_block(pmp->pm_dev, bn + s, NO_READ)) == NULL)
			return EIO;
		memset(b_data(bp), 0, pmp->pm_BytesPerSec);
		lmfs_markdirty(bp);
		put_block(bp);
	}

	return OK;
}

/*===========================================================================*
 *				extend_file				     *
 *===========================================================================*/
int extend_file(struct inode *rip, uint32_t newsize)
{
/* Ensure the file has enough clusters allocated to hold newsize bytes,
 * allocating and chaining new (zeroed) clusters as needed.  Does not change
 * i_size; the caller does that.
 */
	unsigned long have, need, last, cn, frcn;
	int r, isdir;

	isdir = (rip->i_attrs & ATTR_DIRECTORY) ? 1 : 0;

	need = de_clcount(pmp, newsize);
	if (need == 0)
		return OK;

	/* Count clusters currently in the chain and find the last one. */
	have = 0;
	last = 0;
	if (rip->i_start >= CLUST_FIRST && rip->i_start <= pmp->pm_maxcluster) {
		cn = rip->i_start;
		for (;;) {
			have++;
			last = cn;
			if ((r = fatentry_get(cn, &cn)) != OK)
				return r;
			if (cn < CLUST_FIRST || cn > pmp->pm_maxcluster ||
			    MSDOSFSEOF(cn, pmp->pm_fatmask))
				break;
		}
	}

	while (have < need) {
		if ((r = cluster_alloc(last, &cn)) != OK)
			return r;
		if (isdir && (r = zero_cluster(cn)) != OK)
			return r;
		if (rip->i_start < CLUST_FIRST ||
		    rip->i_start > pmp->pm_maxcluster) {
			rip->i_start = cn;	/* first cluster of the file */
			fc_init(rip);
		}
		last = cn;
		have++;
	}

	(void) frcn;
	return OK;
}

/*===========================================================================*
 *				fs_trunc				     *
 *===========================================================================*/
int fs_trunc(ino_t ino_nr, off_t start, off_t end)
{
/* Truncate (or grow) a file so that its size becomes 'start'.  The [start,end)
 * hole-punch variant is treated as a plain truncate to 'start'.
 */
	struct inode *rip;
	unsigned long need, keepcn, cn, i;
	int r;

	if (pmp->pm_rdonly)
		return EROFS;

	if ((rip = find_inode(ino_nr)) == NULL)
		return EINVAL;
	if (rip->i_attrs & ATTR_DIRECTORY)
		return EISDIR;

	if ((off_t) rip->i_size == start)
		return OK;

	if (start > (off_t) rip->i_size) {
		/* Grow: allocate and zero clusters, leaving the gap zeroed. */
		if ((r = extend_file(rip, (uint32_t) start)) != OK)
			return r;
		rip->i_size = (uint32_t) start;
	} else {
		/* Shrink. */
		rip->i_size = (uint32_t) start;
		need = de_clcount(pmp, (uint32_t) start);

		if (need == 0) {
			/* Free the whole chain; file becomes empty. */
			if (rip->i_start >= CLUST_FIRST) {
				if ((r = free_chain(rip->i_start)) != OK)
					return r;
			}
			rip->i_start = 0;
			fc_init(rip);
		} else {
			/* Keep the first 'need' clusters; free the rest. */
			if ((r = chain_nth(rip->i_start, need - 1,
			    &keepcn)) != OK)
				return r;
			if (keepcn != 0) {
				if ((r = fatentry_get(keepcn, &cn)) != OK)
					return r;
				/* Terminate the kept chain. */
				if ((r = fat_set(keepcn,
				    CLUST_EOFE & pmp->pm_fatmask)) != OK)
					return r;
				if (cn >= CLUST_FIRST &&
				    cn <= pmp->pm_maxcluster &&
				    !MSDOSFSEOF(cn, pmp->pm_fatmask)) {
					if ((r = free_chain(cn)) != OK)
						return r;
				}
			}
			fc_init(rip);
		}
	}

	rip->i_mtime = vfat_now();
	(void) i;

	return update_direntry(rip);
}

/*===========================================================================*
 *				fs_utime				     *
 *===========================================================================*/
int fs_utime(ino_t ino_nr, struct timespec *atime, struct timespec *mtime)
{
/* Set a file's modification time.  FAT has no sub-second or full atime, so we
 * honour mtime (and treat atime as advisory).
 */
	struct inode *rip;

	(void) atime;

	if (pmp->pm_rdonly)
		return EROFS;
	if ((rip = find_inode(ino_nr)) == NULL)
		return EINVAL;

	if (mtime->tv_nsec == UTIME_NOW)
		rip->i_mtime = vfat_now();
	else if (mtime->tv_nsec != UTIME_OMIT)
		rip->i_mtime = mtime->tv_sec;

	return update_direntry(rip);
}

/*===========================================================================*
 *				fs_chmod				     *
 *===========================================================================*/
int fs_chmod(ino_t ino_nr, mode_t *mode)
{
/* The only permission bit FAT can represent is "read-only".  Map the absence
 * of owner write permission to ATTR_READONLY and report back the mode we can
 * actually honour.
 */
	struct inode *rip;
	int r;

	if (pmp->pm_rdonly)
		return EROFS;
	if ((rip = find_inode(ino_nr)) == NULL)
		return EINVAL;

	if (*mode & S_IWUSR)
		rip->i_attrs &= ~ATTR_READONLY;
	else
		rip->i_attrs |= ATTR_READONLY;

	node_to_mode(rip);
	*mode = rip->i_mode;

	if ((r = update_direntry(rip)) != OK)
		return r;

	return OK;
}

/*===========================================================================*
 *				update_fsinfo				     *
 *===========================================================================*/
void update_fsinfo(void)
{
/* Write the current free-cluster count and next-free hint into the FAT32
 * FSInfo block.  No-op for FAT12/16 or if there is no FSInfo block.
 */
	struct buf *bp;
	struct fsinfo *fp;

	if (pmp == NULL || pmp->pm_rdonly || !FAT32(pmp) || pmp->pm_fsinfo == 0)
		return;

	if ((bp = get_block(pmp->pm_dev, pmp->pm_fsinfo, NORMAL)) == NULL)
		return;

	fp = (struct fsinfo *) b_data(bp);
	putulong(fp->fsinfree, pmp->pm_freeclustercount);
	putulong(fp->fsinxtfree, pmp->pm_nxtfree);
	lmfs_markdirty(bp);
	put_block(bp);
}

/*===========================================================================*
 *				fs_sync					     *
 *===========================================================================*/
void fs_sync(void)
{
	update_fsinfo();
	lmfs_flushall();
}
