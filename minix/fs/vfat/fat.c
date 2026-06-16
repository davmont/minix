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
 *				chain_nth				     *
 *===========================================================================*/
int chain_nth(unsigned long start, unsigned long frcn, unsigned long *cnp)
{
/* Return in *cnp the frcn-th cluster (0-based) of the chain beginning at
 * cluster 'start', or 0 if frcn is past the end of the chain.
 */
	unsigned long cn, nextcn, i;
	int r;

	*cnp = 0;
	cn = start;
	if (cn < CLUST_FIRST || cn > pmp->pm_maxcluster)
		return OK;		/* no clusters */

	for (i = 0; i < frcn; i++) {
		if ((r = fatentry_get(cn, &nextcn)) != OK)
			return r;
		if (nextcn < CLUST_FIRST || nextcn > pmp->pm_maxcluster ||
		    MSDOSFSEOF(nextcn, pmp->pm_fatmask))
			return OK;	/* past end of chain */
		cn = nextcn;
	}

	*cnp = cn;
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

/*===========================================================================*
 *				write_fat_bytes				     *
 *===========================================================================*/
static int write_fat_bytes(unsigned long sec, unsigned long off, int n,
	unsigned long val)
{
/* Write 'n' little-endian bytes of 'val' at sector 'sec', byte offset 'off',
 * crossing sector boundaries as needed.  Read-modify-write through the cache.
 */
	struct buf *bp;
	int i;

	for (i = 0; i < n; i++) {
		if (off >= pmp->pm_BytesPerSec) {
			sec++;
			off = 0;
		}
		if ((bp = get_block(pmp->pm_dev, sec, NORMAL)) == NULL)
			return EIO;
		b_data(bp)[off] = (char)(val & 0xff);
		lmfs_markdirty(bp);
		put_block(bp);
		val >>= 8;
		off++;
	}

	return OK;
}

/*===========================================================================*
 *				fat_set_one				     *
 *===========================================================================*/
static int fat_set_one(unsigned long fatbase, unsigned long cn,
	unsigned long val)
{
/* Set cluster cn's FAT entry to val in the single FAT copy starting at sector
 * fatbase.  Preserves the bits a FAT entry does not own (FAT12 nibble, FAT32
 * top 4 bits).
 */
	unsigned long byteoffset, sec, off, cur;
	int r;

	byteoffset = FATOFS(pmp, cn);
	sec = fatbase + byteoffset / pmp->pm_BytesPerSec;
	off = byteoffset % pmp->pm_BytesPerSec;

	if (FAT32(pmp)) {
		struct buf *bp;
		unsigned long ent = 0, sec2 = sec, off2 = off;
		int i;

		/* Read current 4 bytes to preserve the top nibble. */
		if ((r = read_fat_bytes(sec, off, 4, &cur)) != OK)
			return r;
		ent = (cur & ~FAT32_MASK) | (val & FAT32_MASK);
		for (i = 0; i < 4; i++) {
			if (off2 >= pmp->pm_BytesPerSec) { sec2++; off2 = 0; }
			if ((bp = get_block(pmp->pm_dev, sec2, NORMAL)) == NULL)
				return EIO;
			b_data(bp)[off2] = (char)(ent & 0xff);
			lmfs_markdirty(bp);
			put_block(bp);
			ent >>= 8;
			off2++;
		}
		return OK;
	} else if (FAT16(pmp)) {
		return write_fat_bytes(sec, off, 2, val & 0xffff);
	} else {	/* FAT12 */
		unsigned long two;

		if ((r = read_fat_bytes(sec, off, 2, &two)) != OK)
			return r;
		if (cn & 1)
			two = (two & 0x000f) | ((val & 0x0fff) << 4);
		else
			two = (two & 0xf000) | (val & 0x0fff);
		return write_fat_bytes(sec, off, 2, two);
	}
}

/*===========================================================================*
 *				fat_set					     *
 *===========================================================================*/
int fat_set(unsigned long cn, unsigned long val)
{
/* Set cluster cn's FAT entry to val in all FAT copies. */
	unsigned int f;
	int r;

	if (cn < CLUST_FIRST || cn > pmp->pm_maxcluster)
		return EINVAL;

	for (f = 0; f < pmp->pm_FATs; f++) {
		unsigned long fatbase = pmp->pm_fatblk + f * pmp->pm_FATsecs;

		if ((r = fat_set_one(fatbase, cn, val)) != OK)
			return r;
	}

	return OK;
}

/*===========================================================================*
 *				fill_inusemap				     *
 *===========================================================================*/
int fill_inusemap(void)
{
/* Allocate and populate the in-use cluster bitmap by scanning the FAT.  A
 * cluster whose FAT entry is zero (CLUST_FREE) is free; everything else is
 * marked in use.  Also computes the free-cluster count.
 */
	unsigned long cn, val, words;
	int r;

	words = (pmp->pm_maxcluster + N_INUSEBITS) / N_INUSEBITS;
	if ((pmp->pm_inusemap = malloc(words * sizeof(unsigned int))) == NULL)
		return ENOMEM;

	/* Start with everything in use; clear free clusters below. */
	for (cn = 0; cn < words; cn++)
		pmp->pm_inusemap[cn] = (unsigned int) -1;

	pmp->pm_freeclustercount = 0;
	for (cn = CLUST_FIRST; cn <= pmp->pm_maxcluster; cn++) {
		if ((r = fatentry_get(cn, &val)) != OK)
			return r;
		if (val == CLUST_FREE) {
			pmp->pm_inusemap[cn / N_INUSEBITS] &=
			    ~(1U << (cn % N_INUSEBITS));
			pmp->pm_freeclustercount++;
		}
	}

	pmp->pm_nxtfree = CLUST_FIRST;
	return OK;
}

/*===========================================================================*
 *				cluster_alloc				     *
 *===========================================================================*/
int cluster_alloc(unsigned long prev, unsigned long *newcn)
{
/* Allocate one free cluster, mark it EOF, and (if prev is a valid cluster)
 * link prev's FAT entry to it.  Updates the in-use map and free count.
 */
	unsigned long cn;
	int r;

	for (cn = pmp->pm_nxtfree; cn <= pmp->pm_maxcluster; cn++)
		if (!(pmp->pm_inusemap[cn / N_INUSEBITS] &
		    (1U << (cn % N_INUSEBITS))))
			goto found;
	for (cn = CLUST_FIRST; cn < pmp->pm_nxtfree; cn++)
		if (!(pmp->pm_inusemap[cn / N_INUSEBITS] &
		    (1U << (cn % N_INUSEBITS))))
			goto found;
	return ENOSPC;

found:
	if ((r = fat_set(cn, CLUST_EOFE & pmp->pm_fatmask)) != OK)
		return r;
	if (prev >= CLUST_FIRST && prev <= pmp->pm_maxcluster)
		if ((r = fat_set(prev, cn)) != OK)
			return r;

	pmp->pm_inusemap[cn / N_INUSEBITS] |= 1U << (cn % N_INUSEBITS);
	pmp->pm_freeclustercount--;
	pmp->pm_nxtfree = cn + 1;

	*newcn = cn;
	return OK;
}

/*===========================================================================*
 *				free_chain				     *
 *===========================================================================*/
int free_chain(unsigned long startcn)
{
/* Free the entire cluster chain beginning at startcn. */
	unsigned long cn, next;
	int r;

	cn = startcn;
	while (cn >= CLUST_FIRST && cn <= pmp->pm_maxcluster) {
		if ((r = fatentry_get(cn, &next)) != OK)
			return r;
		if ((r = fat_set(cn, CLUST_FREE)) != OK)
			return r;
		pmp->pm_inusemap[cn / N_INUSEBITS] &=
		    ~(1U << (cn % N_INUSEBITS));
		pmp->pm_freeclustercount++;
		if (cn < pmp->pm_nxtfree)
			pmp->pm_nxtfree = cn;

		if (MSDOSFSEOF(next, pmp->pm_fatmask))
			break;
		cn = next;
	}

	return OK;
}

/*===========================================================================*
 *				entry_sector				     *
 *===========================================================================*/
unsigned long entry_sector(unsigned long dirclust, unsigned long diroffset)
{
/* Return the device sector holding the directory entry at byte offset
 * diroffset within the directory whose identity cluster is dirclust
 * (MSDOSFSROOT for the FAT12/16 fixed root). */
	unsigned long cn, frcn;
	int r;

	if (dirclust == MSDOSFSROOT && !FAT32(pmp))
		return pmp->pm_rootdirblk + diroffset / pmp->pm_BytesPerSec;

	frcn = diroffset / pmp->pm_bpcluster;
	if ((r = chain_nth(dirclust, frcn, &cn)) != OK || cn == 0)
		return 0;

	return cntobn(pmp, cn) +
	    (diroffset % pmp->pm_bpcluster) / pmp->pm_BytesPerSec;
}
