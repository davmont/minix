/* FAT (File Allocation Table) cluster-chain logic for the vfat server.
 *
 * Read-only subset derived from NetBSD's msdosfs_fat.c (pcbmap/fatentry),
 * using the libminixfs block cache instead of the BSD buffer cache.
 */
#include "fs.h"

/*===========================================================================*
 *				fc_init					     *
 *===========================================================================*/
void fc_init(struct inode *rip)
{
/* Invalidate the per-file cluster cache. */
	int i;

	for (i = 0; i < FC_SIZE; i++) {
		rip->i_fc[i].fc_frcn = FCE_EMPTY;
		rip->i_fc[i].fc_fsrcn = FCE_EMPTY;
	}
}

/*===========================================================================*
 *				read_fat_bytes				     *
 *===========================================================================*/
static int read_fat_bytes(unsigned long sec, unsigned long off, int n,
	unsigned long *valp)
{
/* Read 'n' little-endian bytes starting at sector 'sec', byte offset 'off',
 * crossing into the following sector(s) as needed.  Each access hits the
 * block cache, so the repeated get_block() calls are cheap.
 */
	struct buf *bp;
	unsigned long v = 0;
	int i;

	for (i = 0; i < n; i++) {
		if (off >= pmp->pm_BytesPerSec) {
			sec++;
			off = 0;
		}
		if ((bp = get_block(pmp->pm_dev, sec, NORMAL)) == NULL)
			return EIO;
		v |= ((unsigned long)(uint8_t) b_data(bp)[off]) << (8 * i);
		put_block(bp);
		off++;
	}

	*valp = v;
	return OK;
}

/*===========================================================================*
 *				fatentry_get				     *
 *===========================================================================*/
int fatentry_get(unsigned long cn, unsigned long *outcn)
{
/* Return the FAT entry (next cluster, or an EOF/bad marker) for cluster cn. */
	unsigned long byteoffset, sec, off, val;
	int r;

	byteoffset = FATOFS(pmp, cn);
	sec = pmp->pm_fatblk + byteoffset / pmp->pm_BytesPerSec;
	off = byteoffset % pmp->pm_BytesPerSec;

	if (FAT32(pmp)) {
		if ((r = read_fat_bytes(sec, off, 4, &val)) != OK)
			return r;
		val &= FAT32_MASK;
	} else if (FAT16(pmp)) {
		if ((r = read_fat_bytes(sec, off, 2, &val)) != OK)
			return r;
	} else {	/* FAT12 */
		if ((r = read_fat_bytes(sec, off, 2, &val)) != OK)
			return r;
		if (cn & 1)
			val >>= 4;
		else
			val &= 0x0fff;
	}

	*outcn = val;
	return OK;
}

/*===========================================================================*
 *				bmap					     *
 *===========================================================================*/
int bmap(struct inode *rip, unsigned long frcn, unsigned long *bnp,
	unsigned long *cnp, unsigned long *sizep)
{
/* Map a file-relative cluster number (frcn) to a device sector number (*bnp)
 * and absolute cluster number (*cnp).  On reaching the end of the chain, *bnp
 * and *cnp are set to 0 (no such cluster).  Not used for the FAT12/16 root
 * directory, which is a flat sector range (see direntry.c / read.c).
 */
	unsigned long cn, nextcn, i;
	int r;

	if (sizep != NULL)
		*sizep = pmp->pm_bpcluster;
	*bnp = 0;
	*cnp = 0;

	cn = rip->i_start;
	if (cn < CLUST_FIRST || cn > pmp->pm_maxcluster)
		return OK;		/* empty file: no clusters */

	/* Resume from the cluster cache when possible. */
	i = 0;
	if (rip->i_fc[FC_LASTMAP].fc_frcn != FCE_EMPTY &&
	    frcn >= rip->i_fc[FC_LASTMAP].fc_frcn) {
		i = rip->i_fc[FC_LASTMAP].fc_frcn;
		cn = rip->i_fc[FC_LASTMAP].fc_fsrcn;
	}

	for (; i < frcn; i++) {
		if ((r = fatentry_get(cn, &nextcn)) != OK)
			return r;
		if (nextcn < CLUST_FIRST || nextcn > pmp->pm_maxcluster ||
		    MSDOSFSEOF(nextcn, pmp->pm_fatmask))
			return OK;	/* past end of chain */
		cn = nextcn;
	}

	rip->i_fc[FC_LASTMAP].fc_frcn = frcn;
	rip->i_fc[FC_LASTMAP].fc_fsrcn = cn;

	*cnp = cn;
	*bnp = cntobn(pmp, cn);
	return OK;
}
