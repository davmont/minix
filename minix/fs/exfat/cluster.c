/* Cluster and FAT handling for the exFAT server.
 *
 * exFAT keeps a 32-bit FAT, but a file/directory whose stream entry has the
 * NoFatChain flag occupies a contiguous run of clusters and the FAT must not
 * be consulted for it.  All chain walks below honour that flag.
 */
#include "fs.h"

/*===========================================================================*
 *				cluster_to_sector			     *
 *===========================================================================*/
uint64_t cluster_to_sector(uint32_t cn)
{
/* First device sector of data cluster cn (cn >= 2). */
	return pmp->pm_cluster_heap_off +
	    ((uint64_t)(cn - EXFAT_CLUST_FIRST) << pmp->pm_clus_shift);
}

/*===========================================================================*
 *				fat_get					     *
 *===========================================================================*/
int fat_get(uint32_t cn, uint32_t *nextp)
{
/* Return the FAT entry for cluster cn (the next cluster in its chain).  A
 * 32-bit entry never straddles a sector boundary (sector size is a multiple
 * of 4), so a single block read suffices. */
	struct buf *bp;
	uint64_t byteoff, sec;
	unsigned secoff;

	if (cn < EXFAT_CLUST_FIRST || cn > pmp->pm_max_cluster)
		return EINVAL;

	byteoff = (uint64_t) cn * 4;
	sec = pmp->pm_fat_off + (byteoff >> pmp->pm_sec_shift);
	secoff = (unsigned)(byteoff & (pmp->pm_bytes_per_sec - 1));

	if ((bp = get_block(pmp->pm_dev, sec, NORMAL)) == NULL)
		return EIO;
	*nextp = ex_get32((uint8_t *) b_data(bp) + secoff);
	put_block(bp);

	return OK;
}

/*===========================================================================*
 *				chain_nth				     *
 *===========================================================================*/
int chain_nth(uint32_t start, int contig, uint64_t frcn, uint32_t *cnp)
{
/* Return the cluster number of the frcn-th cluster (0-based) of the chain that
 * begins at cluster 'start'.  For a contiguous run this is simple arithmetic;
 * otherwise the FAT is walked.  Returns ENOENT if frcn is past the end of the
 * chain (i.e. a hole / past EOF). */
	uint32_t cn = start;
	uint64_t i;

	if (start < EXFAT_CLUST_FIRST || start > pmp->pm_max_cluster)
		return EINVAL;

	if (contig) {
		cn = (uint32_t)(start + frcn);
		if (cn > pmp->pm_max_cluster)
			return ENOENT;
		*cnp = cn;
		return OK;
	}

	for (i = 0; i < frcn; i++) {
		uint32_t next;
		int r;

		if ((r = fat_get(cn, &next)) != OK)
			return r;
		if (next == EXFAT_CLUST_EOF || next < EXFAT_CLUST_FIRST ||
		    next > pmp->pm_max_cluster)
			return ENOENT;		/* end of chain */
		cn = next;
	}

	*cnp = cn;
	return OK;
}

/*===========================================================================*
 *				chain_read				     *
 *===========================================================================*/
int chain_read(uint32_t start, int contig, uint64_t off, void *buf, size_t len)
{
/* Read 'len' bytes starting at byte offset 'off' from the cluster chain that
 * begins at cluster 'start' into 'buf'.  Used for metadata (up-case table,
 * allocation bitmap). */
	uint8_t *dst = buf;

	while (len > 0) {
		uint64_t frcn = off >> pmp->pm_clus_byte_shift;
		unsigned cloff = (unsigned)(off & (pmp->pm_bytes_per_clus - 1));
		uint32_t cn;
		uint64_t sec;
		unsigned secoff;
		size_t chunk;
		struct buf *bp;
		int r;

		if ((r = chain_nth(start, contig, frcn, &cn)) != OK)
			return r;

		sec = cluster_to_sector(cn) + (cloff >> pmp->pm_sec_shift);
		secoff = (unsigned)(cloff & (pmp->pm_bytes_per_sec - 1));
		chunk = pmp->pm_bytes_per_sec - secoff;
		if (chunk > len)
			chunk = len;

		if ((bp = get_block(pmp->pm_dev, sec, NORMAL)) == NULL)
			return EIO;
		memcpy(dst, b_data(bp) + secoff, chunk);
		put_block(bp);

		dst += chunk;
		off += chunk;
		len -= chunk;
	}
	return OK;
}

/*===========================================================================*
 *				bmap					     *
 *===========================================================================*/
int bmap(struct inode *rip, uint64_t frcn, uint64_t *secp)
{
/* Map the frcn-th cluster of inode rip's data to its first device sector.
 * Returns ENOENT if frcn is past the file's cluster chain. */
	uint32_t cn;
	int r;

	if (rip->i_start < EXFAT_CLUST_FIRST)
		return ENOENT;		/* empty file/dir: no clusters */

	if ((r = chain_nth(rip->i_start, IS_CONTIG(rip), frcn, &cn)) != OK)
		return r;

	*secp = cluster_to_sector(cn);
	return OK;
}

/*===========================================================================*
 *				fat_set					     *
 *===========================================================================*/
int fat_set(uint32_t cn, uint32_t val)
{
/* Write the FAT entry for cluster cn.  exFAT normally has a single FAT. */
	struct buf *bp;
	uint64_t byteoff, sec;
	unsigned secoff;

	if (cn < EXFAT_CLUST_FIRST || cn > pmp->pm_max_cluster)
		return EINVAL;

	byteoff = (uint64_t) cn * 4;
	sec = pmp->pm_fat_off + (byteoff >> pmp->pm_sec_shift);
	secoff = (unsigned)(byteoff & (pmp->pm_bytes_per_sec - 1));

	if ((bp = get_block(pmp->pm_dev, sec, NORMAL)) == NULL)
		return EIO;
	ex_put32((uint8_t *) b_data(bp) + secoff, val);
	lmfs_markdirty(bp);
	put_block(bp);

	return OK;
}

/*===========================================================================*
 *				chain_write				     *
 *===========================================================================*/
int chain_write(uint32_t start, int contig, uint64_t off, const void *buf,
	size_t len)
{
/* Write 'len' bytes at byte offset 'off' into the cluster chain starting at
 * 'start' (read-modify-write at sector granularity). */
	const uint8_t *src = buf;

	while (len > 0) {
		uint64_t frcn = off >> pmp->pm_clus_byte_shift;
		unsigned cloff = (unsigned)(off & (pmp->pm_bytes_per_clus - 1));
		uint32_t cn;
		uint64_t sec;
		unsigned secoff;
		size_t chunk;
		struct buf *bp;
		int r;

		if ((r = chain_nth(start, contig, frcn, &cn)) != OK)
			return r;

		sec = cluster_to_sector(cn) + (cloff >> pmp->pm_sec_shift);
		secoff = (unsigned)(cloff & (pmp->pm_bytes_per_sec - 1));
		chunk = pmp->pm_bytes_per_sec - secoff;
		if (chunk > len)
			chunk = len;

		if ((bp = get_block(pmp->pm_dev, sec, NORMAL)) == NULL)
			return EIO;
		memcpy(b_data(bp) + secoff, src, chunk);
		lmfs_markdirty(bp);
		put_block(bp);

		src += chunk;
		off += chunk;
		len -= chunk;
	}
	return OK;
}

/*===========================================================================*
 *				bitmap_bit				     *
 *===========================================================================*/
static int bitmap_bit(uint32_t cn, int set, int clear)
{
/* Read, and optionally set or clear, the allocation-bitmap bit for cluster cn
 * (bit cn-2).  Returns 1 if the bit was set (before any change), 0 if clear,
 * or a negative errno. */
	struct buf *bp;
	uint64_t bit, sec;
	unsigned secoff, mask;
	int was;

	if (cn < EXFAT_CLUST_FIRST || cn > pmp->pm_max_cluster)
		return -EINVAL;

	bit = cn - EXFAT_CLUST_FIRST;
	sec = cluster_to_sector(pmp->pm_bitmap_cluster) +
	    ((bit / 8) >> pmp->pm_sec_shift);
	secoff = (unsigned)((bit / 8) & (pmp->pm_bytes_per_sec - 1));
	mask = 1u << (bit & 7);

	if ((bp = get_block(pmp->pm_dev, sec, NORMAL)) == NULL)
		return -EIO;
	was = (((uint8_t *) b_data(bp))[secoff] & mask) ? 1 : 0;
	if (set && !was) {
		((uint8_t *) b_data(bp))[secoff] |= mask;
		lmfs_markdirty(bp);
	} else if (clear && was) {
		((uint8_t *) b_data(bp))[secoff] &= ~mask;
		lmfs_markdirty(bp);
	}
	put_block(bp);

	return was;
}

/*===========================================================================*
 *				bitmap_set / bitmap_clear		     *
 *===========================================================================*/
int bitmap_set(uint32_t cn)
{
	int was = bitmap_bit(cn, 1, 0);

	if (was < 0)
		return -was;
	if (!was && pmp->pm_free_clusters > 0)
		pmp->pm_free_clusters--;
	return OK;
}

int bitmap_clear(uint32_t cn)
{
	int was = bitmap_bit(cn, 0, 1);

	if (was < 0)
		return -was;
	if (was)
		pmp->pm_free_clusters++;
	if (cn < pmp->pm_next_free)
		pmp->pm_next_free = cn;
	return OK;
}

/*===========================================================================*
 *				cluster_alloc				     *
 *===========================================================================*/
int cluster_alloc(uint32_t prev, uint32_t *newcn)
{
/* Allocate one free cluster (scanning the allocation bitmap from the next-free
 * hint), mark it used and end-of-chain, and link it after 'prev' (0 = none).
 * The new cluster's data is not zeroed here. */
	uint32_t cn, start;
	int r, was;

	start = pmp->pm_next_free;
	if (start < EXFAT_CLUST_FIRST)
		start = EXFAT_CLUST_FIRST;

	for (cn = start; cn <= pmp->pm_max_cluster; cn++) {
		if ((was = bitmap_bit(cn, 0, 0)) < 0)
			return -was;
		if (!was)
			goto found;
	}
	for (cn = EXFAT_CLUST_FIRST; cn < start; cn++) {
		if ((was = bitmap_bit(cn, 0, 0)) < 0)
			return -was;
		if (!was)
			goto found;
	}
	return ENOSPC;

found:
	if ((r = bitmap_set(cn)) != OK)
		return r;
	if ((r = fat_set(cn, EXFAT_CLUST_EOF)) != OK)
		return r;
	if (prev >= EXFAT_CLUST_FIRST && (r = fat_set(prev, cn)) != OK)
		return r;

	pmp->pm_next_free = cn + 1;
	*newcn = cn;
	return OK;
}

/*===========================================================================*
 *				free_chain				     *
 *===========================================================================*/
int free_chain(uint32_t startcn, int contig, uint64_t nclusters)
{
/* Free a cluster chain (or a contiguous run of nclusters) starting at startcn,
 * clearing both the FAT entries and the allocation bitmap. */
	uint32_t cn = startcn;
	uint64_t i;
	int r;

	if (startcn < EXFAT_CLUST_FIRST)
		return OK;

	if (contig) {
		for (i = 0; i < nclusters; i++) {
			if (cn + i > pmp->pm_max_cluster)
				break;
			if ((r = bitmap_clear((uint32_t)(cn + i))) != OK)
				return r;
		}
		return OK;
	}

	while (cn >= EXFAT_CLUST_FIRST && cn <= pmp->pm_max_cluster) {
		uint32_t next;

		if ((r = fat_get(cn, &next)) != OK)
			return r;
		if ((r = fat_set(cn, EXFAT_CLUST_FREE)) != OK)
			return r;
		if ((r = bitmap_clear(cn)) != OK)
			return r;
		if (next == EXFAT_CLUST_EOF || next < EXFAT_CLUST_FIRST ||
		    next > pmp->pm_max_cluster)
			break;
		cn = next;
	}
	return OK;
}

/*===========================================================================*
 *				chain_clusters				     *
 *===========================================================================*/
uint64_t chain_clusters(uint32_t start, int contig, uint64_t bytes)
{
/* Number of clusters a chain of 'bytes' bytes occupies. */
	(void) start;
	(void) contig;
	return (bytes + pmp->pm_bytes_per_clus - 1) >> pmp->pm_clus_byte_shift;
}

/*===========================================================================*
 *				convert_to_chained			     *
 *===========================================================================*/
int convert_to_chained(struct inode *rip)
{
/* A NoFatChain (contiguous) file is about to be extended or otherwise needs
 * the FAT; materialize a real FAT chain over its existing clusters and clear
 * the flag, so subsequent allocation can fragment it. */
	uint64_t n, i;
	int r;

	if (!IS_CONTIG(rip) || rip->i_start < EXFAT_CLUST_FIRST)
		return OK;

	n = chain_clusters(rip->i_start, 1, rip->i_size);
	for (i = 0; i < n; i++) {
		uint32_t cn = (uint32_t)(rip->i_start + i);
		uint32_t val = (i + 1 < n) ? (uint32_t)(cn + 1) : EXFAT_CLUST_EOF;

		if ((r = fat_set(cn, val)) != OK)
			return r;
	}

	rip->i_secflags &= ~EXFAT_SECFLAG_NO_FAT_CHAIN;
	return OK;
}

/*===========================================================================*
 *				bitmap_count_free			     *
 *===========================================================================*/
int bitmap_count_free(void)
{
/* Count the free clusters by scanning the allocation bitmap (1 bit per
 * cluster, cluster 2 == bit 0; a set bit means in use).  Reads the bitmap in
 * sector-sized chunks to bound memory use. */
	uint8_t *secbuf;
	uint64_t done;
	uint32_t used = 0, total = pmp->pm_cluster_count;
	unsigned secsz = pmp->pm_bytes_per_sec;

	pmp->pm_free_clusters = 0;
	pmp->pm_next_free = EXFAT_CLUST_FIRST;

	if (pmp->pm_bitmap_cluster < EXFAT_CLUST_FIRST)
		return EINVAL;		/* no allocation bitmap found */

	if ((secbuf = malloc(secsz)) == NULL)
		return ENOMEM;

	for (done = 0; done < total; ) {
		uint64_t byteoff = done / 8;
		unsigned i, nbits;
		int r;

		if ((r = chain_read(pmp->pm_bitmap_cluster, 0, byteoff,
		    secbuf, secsz)) != OK) {
			free(secbuf);
			return r;
		}

		nbits = secsz * 8;
		if (nbits > total - done)
			nbits = (unsigned)(total - done);

		for (i = 0; i < nbits; i++) {
			if (secbuf[i >> 3] & (1 << (i & 7)))
				used++;
		}
		done += nbits;
	}

	free(secbuf);
	pmp->pm_free_clusters = total - used;
	return OK;
}
