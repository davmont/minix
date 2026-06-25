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
