/* Write support for the exFAT server: cluster (de)allocation via the on-disk
 * allocation bitmap, directory entry-set creation/removal (with SetChecksum and
 * NameHash), file extend/truncate, and the create/mkdir/unlink/rmdir/rename/
 * utime/chmod handlers.
 */
#include "fs.h"
#include <sys/stat.h>
#include <sys/time.h>

/*===========================================================================*
 *				exfat_now				     *
 *===========================================================================*/
time_t exfat_now(void)
{
	return clock_time(NULL);
}

/*===========================================================================*
 *				set_checksum				     *
 *===========================================================================*/
static uint16_t set_checksum(const uint8_t *buf, size_t nbytes)
{
/* SetChecksum over a whole entry set, skipping the File entry's own checksum
 * field (bytes 2 and 3). */
	uint16_t cs = 0;
	size_t i;

	for (i = 0; i < nbytes; i++) {
		if (i == 2 || i == 3)
			continue;
		cs = (uint16_t)(((cs << 15) | (cs >> 1)) + buf[i]);
	}
	return cs;
}

/*===========================================================================*
 *				dir_cluster_count			     *
 *===========================================================================*/
static uint64_t dir_cluster_count(struct inode *dir)
{
/* Number of clusters allocated to directory 'dir'. */
	uint32_t cn = dir->i_start;
	uint64_t n = 0;

	if (cn < EXFAT_CLUST_FIRST)
		return 0;
	if (IS_CONTIG(dir))
		return (dir->i_size + pmp->pm_bytes_per_clus - 1) >>
		    pmp->pm_clus_byte_shift;

	while (cn >= EXFAT_CLUST_FIRST && cn <= pmp->pm_max_cluster) {
		uint32_t next;

		n++;
		if (fat_get(cn, &next) != OK)
			break;
		if (next == EXFAT_CLUST_EOF || next < EXFAT_CLUST_FIRST ||
		    next > pmp->pm_max_cluster)
			break;
		cn = next;
	}
	return n;
}

/*===========================================================================*
 *				zero_cluster				     *
 *===========================================================================*/
int zero_cluster(uint32_t cn)
{
/* Zero every sector of data cluster cn. */
	uint64_t bn = cluster_to_sector(cn);
	unsigned s;
	struct buf *bp;

	for (s = 0; s < pmp->pm_sec_per_clus; s++) {
		if ((bp = get_block(pmp->pm_dev, bn + s, NO_READ)) == NULL)
			return EIO;
		memset(b_data(bp), 0, pmp->pm_bytes_per_sec);
		lmfs_markdirty(bp);
		put_block(bp);
	}
	return OK;
}

/*===========================================================================*
 *				extend_dir				     *
 *===========================================================================*/
static int extend_dir(struct inode *dir, uint64_t min_bytes)
{
/* Grow directory 'dir' so it has at least min_bytes of (zeroed) capacity. */
	uint64_t have, count;
	int r;

	if (IS_CONTIG(dir) && (r = convert_to_chained(dir)) != OK)
		return r;

	count = dir_cluster_count(dir);
	have = count * pmp->pm_bytes_per_clus;

	while (have < min_bytes) {
		uint32_t last = 0, cn;

		if (count > 0 &&
		    (r = chain_nth(dir->i_start, 0, count - 1, &last)) != OK)
			return r;
		if ((r = cluster_alloc(last, &cn)) != OK)
			return r;
		if ((r = zero_cluster(cn)) != OK)
			return r;
		if (dir->i_start < EXFAT_CLUST_FIRST) {
			dir->i_start = cn;
			dir->i_secflags = EXFAT_SECFLAG_ALLOC_POSSIBLE;
		}
		count++;
		have += pmp->pm_bytes_per_clus;
	}

	/* A non-root directory records its allocated size in its own entry. */
	if (!dir->i_root) {
		dir->i_size = have;
		dir->i_valid = have;
		if ((r = update_direntry(dir)) != OK)
			return r;
	}
	return OK;
}

/*===========================================================================*
 *				update_direntry				     *
 *===========================================================================*/
int update_direntry(struct inode *rip)
{
/* Write back the mutable fields of an inode into its on-disk File + Stream
 * directory entries (in the parent directory), recomputing the SetChecksum. */
	uint8_t setbuf[19 * EXFAT_DENTRY_SIZE];
	struct exfat_file_entry *fe;
	struct exfat_stream_entry *se;
	uint32_t stamp;
	uint8_t inc, tz;
	unsigned total;
	int contig, r;

	if (rip->i_root)
		return OK;

	total = 1u + rip->i_secondary_count;
	if (total * EXFAT_DENTRY_SIZE > sizeof(setbuf))
		return EINVAL;

	contig = (rip->i_parent_secflags & EXFAT_SECFLAG_NO_FAT_CHAIN) ? 1 : 0;
	if ((r = chain_read(rip->i_dirclust, contig, rip->i_diroffset, setbuf,
	    total * EXFAT_DENTRY_SIZE)) != OK)
		return r;

	fe = (struct exfat_file_entry *) setbuf;
	se = (struct exfat_stream_entry *) (setbuf + EXFAT_DENTRY_SIZE);
	if (fe->type != EXFAT_ENTRY_FILE || se->type != EXFAT_ENTRY_STREAM)
		return EIO;

	ex_put16(fe->attributes, rip->i_attrs);
	timespec_to_exfat(rip->i_mtime, &stamp, &inc, &tz);
	ex_put32(fe->modify_time, stamp);
	fe->modify_10ms = inc;
	fe->modify_utc_offset = tz;

	se->flags = rip->i_secflags;
	ex_put32(se->first_cluster, rip->i_start);
	ex_put64(se->data_length, rip->i_size);
	ex_put64(se->valid_data_length, rip->i_valid);

	ex_put16(fe->set_checksum, set_checksum(setbuf, total * EXFAT_DENTRY_SIZE));

	return chain_write(rip->i_dirclust, contig, rip->i_diroffset, setbuf,
	    total * EXFAT_DENTRY_SIZE);
}

/*===========================================================================*
 *				build_fileset				     *
 *===========================================================================*/
static unsigned build_fileset(uint8_t *buf, const uint16_t *wname, int nlen,
	uint16_t attrs, uint32_t first, uint64_t size, uint64_t valid,
	uint8_t secflags, time_t mtime)
{
/* Lay out a complete File + Stream + Name entry set into buf.  Returns the set
 * size in bytes. */
	struct exfat_file_entry *fe = (struct exfat_file_entry *) buf;
	struct exfat_stream_entry *se;
	unsigned nents = (nlen + EXFAT_NAME_CHARS_PER_ENTRY - 1) /
	    EXFAT_NAME_CHARS_PER_ENTRY;
	unsigned total = 2 + nents;
	uint32_t stamp;
	uint8_t inc, tz;
	unsigned i;

	memset(buf, 0, total * EXFAT_DENTRY_SIZE);

	fe->type = EXFAT_ENTRY_FILE;
	fe->secondary_count = (uint8_t)(1 + nents);
	ex_put16(fe->attributes, attrs);
	timespec_to_exfat(mtime, &stamp, &inc, &tz);
	ex_put32(fe->create_time, stamp);
	ex_put32(fe->modify_time, stamp);
	ex_put32(fe->access_time, stamp);

	se = (struct exfat_stream_entry *) (buf + EXFAT_DENTRY_SIZE);
	se->type = EXFAT_ENTRY_STREAM;
	se->flags = secflags;
	se->name_length = (uint8_t) nlen;
	ex_put16(se->name_hash, name_hash(wname, nlen));
	ex_put64(se->valid_data_length, valid);
	ex_put32(se->first_cluster, first);
	ex_put64(se->data_length, size);

	for (i = 0; i < nents; i++) {
		struct exfat_name_entry *ne = (struct exfat_name_entry *)
		    (buf + (2 + i) * EXFAT_DENTRY_SIZE);
		int k;

		ne->type = EXFAT_ENTRY_NAME;
		for (k = 0; k < EXFAT_NAME_CHARS_PER_ENTRY; k++) {
			int idx = i * EXFAT_NAME_CHARS_PER_ENTRY + k;

			if (idx < nlen)
				ex_put16(ne->name + k * 2, wname[idx]);
		}
	}

	ex_put16(fe->set_checksum, set_checksum(buf, total * EXFAT_DENTRY_SIZE));
	return total * EXFAT_DENTRY_SIZE;
}

/*===========================================================================*
 *				find_free_run				     *
 *===========================================================================*/
static int find_free_run(struct inode *dir, unsigned total, uint64_t *placep)
{
/* Find a run of 'total' consecutive free/deleted directory slots, growing the
 * directory if necessary, and return its byte offset. */
	int contig = IS_CONTIG(dir) ? 1 : 0;
	struct exfat_dentry de;
	uint64_t off = 0, run_start = 0;
	unsigned run = 0;
	int r;

	for (;;) {
		r = chain_read(dir->i_start, contig, off, &de, EXFAT_DENTRY_SIZE);
		if (r == ENOENT || (r == OK && de.type == EXFAT_TYPE_END)) {
			/* Free from here to the end of the allocation. */
			uint64_t place = (run > 0) ? run_start : off;

			if ((r = extend_dir(dir, place +
			    (uint64_t) total * EXFAT_DENTRY_SIZE)) != OK)
				return r;
			*placep = place;
			return OK;
		}
		if (r != OK)
			return r;

		if (!(de.type & EXFAT_TYPE_INUSE)) {	/* deleted/free slot */
			if (run == 0)
				run_start = off;
			if (++run >= total) {
				*placep = run_start;
				return OK;
			}
			off += EXFAT_DENTRY_SIZE;
		} else {
			run = 0;
			if (de.type == EXFAT_ENTRY_FILE) {
				struct exfat_file_entry *fe =
				    (struct exfat_file_entry *) &de;
				off += (uint64_t) set_entries_of(fe) *
				    EXFAT_DENTRY_SIZE;
			} else {
				off += EXFAT_DENTRY_SIZE;
			}
		}
	}
}

/*===========================================================================*
 *				createde				     *
 *===========================================================================*/
static int createde(struct inode *dir, const char *name, uint16_t attrs,
	uint32_t first, uint64_t size, uint64_t valid, uint8_t secflags,
	uint64_t *offp)
{
/* Create a directory entry set for 'name' in directory 'dir'. */
	uint16_t wname[EXFAT_NAME_MAX];
	uint8_t setbuf[19 * EXFAT_DENTRY_SIZE];
	uint64_t place;
	unsigned setsz;
	int nlen, total, r;

	if ((nlen = utf8_to_utf16(name, wname, EXFAT_NAME_MAX)) < 0)
		return ENAMETOOLONG;
	total = 2 + (nlen + EXFAT_NAME_CHARS_PER_ENTRY - 1) /
	    EXFAT_NAME_CHARS_PER_ENTRY;
	if ((unsigned)(total * EXFAT_DENTRY_SIZE) > sizeof(setbuf))
		return ENAMETOOLONG;

	setsz = build_fileset(setbuf, wname, nlen, attrs, first, size, valid,
	    secflags, exfat_now());

	if ((r = find_free_run(dir, (unsigned) total, &place)) != OK)
		return r;

	if ((r = chain_write(dir->i_start, IS_CONTIG(dir) ? 1 : 0, place,
	    setbuf, setsz)) != OK)
		return r;

	*offp = place;
	return OK;
}

/*===========================================================================*
 *				removede				     *
 *===========================================================================*/
static int removede(struct inode *dir, uint64_t off)
{
/* Mark the entry set at 'off' deleted by clearing the InUse bit on each of its
 * entries. */
	uint8_t setbuf[19 * EXFAT_DENTRY_SIZE];
	struct exfat_file_entry *fe;
	int contig = IS_CONTIG(dir) ? 1 : 0;
	unsigned total, i;
	int r;

	if ((r = chain_read(dir->i_start, contig, off, setbuf,
	    EXFAT_DENTRY_SIZE)) != OK)
		return r;
	fe = (struct exfat_file_entry *) setbuf;
	if (fe->type != EXFAT_ENTRY_FILE)
		return EIO;
	total = 1u + fe->secondary_count;
	if (total * EXFAT_DENTRY_SIZE > sizeof(setbuf))
		return EINVAL;

	if ((r = chain_read(dir->i_start, contig, off, setbuf,
	    total * EXFAT_DENTRY_SIZE)) != OK)
		return r;
	for (i = 0; i < total; i++)
		setbuf[i * EXFAT_DENTRY_SIZE] &= ~EXFAT_TYPE_INUSE;

	return chain_write(dir->i_start, contig, off, setbuf,
	    total * EXFAT_DENTRY_SIZE);
}

/*===========================================================================*
 *				extend_file				     *
 *===========================================================================*/
int extend_file(struct inode *rip, uint64_t newsize)
{
/* Ensure 'rip' has enough clusters for newsize bytes, allocating (FAT-chained)
 * clusters as needed.  Does not change i_size. */
	uint64_t have, need;
	uint32_t last = 0, cn;
	int r;

	need = (newsize + pmp->pm_bytes_per_clus - 1) >> pmp->pm_clus_byte_shift;
	if (need == 0)
		return OK;

	/* Any growth fragments a contiguous file; chain it first. */
	if (IS_CONTIG(rip) && (r = convert_to_chained(rip)) != OK)
		return r;

	have = (rip->i_start >= EXFAT_CLUST_FIRST) ?
	    chain_clusters(rip->i_start, 0, rip->i_size) : 0;
	if (have > 0 &&
	    (r = chain_nth(rip->i_start, 0, have - 1, &last)) != OK)
		return r;

	while (have < need) {
		if ((r = cluster_alloc(last, &cn)) != OK)
			return r;
		if (rip->i_start < EXFAT_CLUST_FIRST) {
			rip->i_start = cn;
			rip->i_secflags |= EXFAT_SECFLAG_ALLOC_POSSIBLE;
		}
		last = cn;
		have++;
	}
	return OK;
}

/*===========================================================================*
 *				fs_create				     *
 *===========================================================================*/
int fs_create(ino_t dir_nr, char *name, mode_t mode, uid_t uid, gid_t gid,
	struct fsdriver_node *node)
{
	struct inode *dir, *rip;
	struct exfat_file_entry fe;
	struct exfat_stream_entry se;
	uint64_t off, dummy;
	uint16_t attrs;
	int r;

	(void) mode; (void) uid; (void) gid;

	if (pmp->pm_rdonly)
		return EROFS;
	if ((dir = find_inode(dir_nr)) == NULL)
		return EINVAL;
	if (!(dir->i_attrs & EXFAT_ATTR_DIRECTORY))
		return ENOTDIR;
	if (dir_find(dir, name, &dummy, &fe, &se) == OK)
		return EEXIST;

	attrs = EXFAT_ATTR_ARCHIVE;
	if (!(mode & S_IWUSR))
		attrs |= EXFAT_ATTR_READONLY;

	if ((r = createde(dir, name, attrs, 0, 0, 0, 0, &off)) != OK)
		return r;
	if ((r = dir_find(dir, name, &off, &fe, &se)) != OK)
		return r;
	if ((rip = enter_inode(dir, (uint32_t) off, &fe, &se)) == NULL)
		return ENFILE;

	node->fn_ino_nr = rip->i_num;
	node->fn_mode = rip->i_mode;
	node->fn_size = rip->i_size;
	node->fn_uid = pmp->pm_uid;
	node->fn_gid = pmp->pm_gid;
	node->fn_dev = NO_DEV;

	return OK;
}

/*===========================================================================*
 *				fs_mkdir				     *
 *===========================================================================*/
int fs_mkdir(ino_t dir_nr, char *name, mode_t mode, uid_t uid, gid_t gid)
{
	struct inode *dir;
	struct exfat_file_entry fe;
	struct exfat_stream_entry se;
	uint64_t off, dummy;
	uint32_t cn;
	int r;

	(void) mode; (void) uid; (void) gid;

	if (pmp->pm_rdonly)
		return EROFS;
	if ((dir = find_inode(dir_nr)) == NULL)
		return EINVAL;
	if (!(dir->i_attrs & EXFAT_ATTR_DIRECTORY))
		return ENOTDIR;
	if (dir_find(dir, name, &dummy, &fe, &se) == OK)
		return EEXIST;

	/* Allocate and zero the new directory's first cluster. */
	if ((r = cluster_alloc(0, &cn)) != OK)
		return r;
	if ((r = zero_cluster(cn)) != OK)
		return r;

	r = createde(dir, name, EXFAT_ATTR_DIRECTORY, cn,
	    pmp->pm_bytes_per_clus, pmp->pm_bytes_per_clus,
	    EXFAT_SECFLAG_ALLOC_POSSIBLE, &off);
	if (r != OK) {
		(void) free_chain(cn, 1, 1);
		return r;
	}

	return OK;
}

/*===========================================================================*
 *				fs_unlink				     *
 *===========================================================================*/
int fs_unlink(ino_t dir_nr, char *name, int call)
{
	struct inode *dir, *rip;
	struct exfat_file_entry fe;
	struct exfat_stream_entry se;
	uint64_t off;
	uint32_t first;
	uint64_t size;
	uint16_t attrs;
	int contig, isdir, r;

	if (pmp->pm_rdonly)
		return EROFS;
	if ((dir = find_inode(dir_nr)) == NULL)
		return EINVAL;
	if ((r = dir_find(dir, name, &off, &fe, &se)) != OK)
		return r;

	attrs = ex_get16(fe.attributes);
	isdir = (attrs & EXFAT_ATTR_DIRECTORY) ? 1 : 0;
	first = ex_get32(se.first_cluster);
	size = ex_get64(se.data_length);
	contig = (se.flags & EXFAT_SECFLAG_NO_FAT_CHAIN) ? 1 : 0;

	if (call == FSC_UNLINK && isdir)
		return EPERM;
	if (call == FSC_RMDIR && !isdir)
		return ENOTDIR;

	if (isdir) {
		/* Refuse to remove a non-empty directory. */
		if ((rip = enter_inode(dir, (uint32_t) off, &fe, &se)) == NULL)
			return ENFILE;
		r = dir_is_empty(rip);
		put_inode(rip);
		if (!r)
			return ENOTEMPTY;
	}

	if (first >= EXFAT_CLUST_FIRST &&
	    (r = free_chain(first, contig, chain_clusters(first, contig, size)))
	    != OK)
		return r;

	return removede(dir, off);
}

/*===========================================================================*
 *				fs_rename				     *
 *===========================================================================*/
int fs_rename(ino_t old_dir_nr, char *old_name, ino_t new_dir_nr,
	char *new_name)
{
	struct inode *odir, *ndir;
	struct exfat_file_entry fe, tfe;
	struct exfat_stream_entry se, tse;
	uint64_t ooff, noff, dummy;
	uint16_t attrs;
	uint32_t first;
	uint64_t size, valid;
	uint8_t secflags;
	int r;

	if (pmp->pm_rdonly)
		return EROFS;
	if ((odir = find_inode(old_dir_nr)) == NULL ||
	    (ndir = find_inode(new_dir_nr)) == NULL)
		return EINVAL;
	if ((r = dir_find(odir, old_name, &ooff, &fe, &se)) != OK)
		return r;

	attrs = ex_get16(fe.attributes);
	first = ex_get32(se.first_cluster);
	size = ex_get64(se.data_length);
	valid = ex_get64(se.valid_data_length);
	secflags = se.flags;

	/* If the destination exists, it must be removed first (POSIX replace). */
	if (dir_find(ndir, new_name, &noff, &tfe, &tse) == OK) {
		uint16_t tattrs = ex_get16(tfe.attributes);
		uint32_t tfirst = ex_get32(tse.first_cluster);
		uint64_t tsize = ex_get64(tse.data_length);
		int tcontig = (tse.flags & EXFAT_SECFLAG_NO_FAT_CHAIN) ? 1 : 0;
		int tisdir = (tattrs & EXFAT_ATTR_DIRECTORY) ? 1 : 0;
		int sisdir = (attrs & EXFAT_ATTR_DIRECTORY) ? 1 : 0;

		if (tisdir != sisdir)
			return tisdir ? EISDIR : ENOTDIR;
		if (tisdir) {
			struct inode *t = enter_inode(ndir, (uint32_t) noff,
			    &tfe, &tse);
			if (t == NULL)
				return ENFILE;
			r = dir_is_empty(t);
			put_inode(t);
			if (!r)
				return ENOTEMPTY;
		}
		if (tfirst >= EXFAT_CLUST_FIRST &&
		    (r = free_chain(tfirst, tcontig,
		    chain_clusters(tfirst, tcontig, tsize))) != OK)
			return r;
		if ((r = removede(ndir, noff)) != OK)
			return r;
	}

	/* Create the new entry, then drop the old one. */
	if ((r = createde(ndir, new_name, attrs, first, size, valid, secflags,
	    &noff)) != OK)
		return r;
	if ((r = removede(odir, ooff)) != OK)
		return r;

	/* Re-key a cached inode for the moved object to its new location. */
	(void) dummy;
	{
		ino_t oino = make_ino(odir->i_start, (uint32_t) ooff);
		struct inode *rip = find_inode(oino);

		if (rip != NULL) {
			rip->i_num = make_ino(ndir->i_start, (uint32_t) noff);
			rip->i_dirclust = ndir->i_start;
			rip->i_diroffset = (uint32_t) noff;
			rip->i_parent = ndir->i_num;
			rip->i_parent_secflags = ndir->i_secflags;
		}
	}

	return OK;
}

/*===========================================================================*
 *				fs_trunc				     *
 *===========================================================================*/
int fs_trunc(ino_t ino_nr, off_t start, off_t end)
{
	struct inode *rip;
	uint64_t need, have;
	int r;

	(void) end;

	if (pmp->pm_rdonly)
		return EROFS;
	if ((rip = find_inode(ino_nr)) == NULL)
		return EINVAL;
	if (rip->i_attrs & EXFAT_ATTR_DIRECTORY)
		return EISDIR;
	if ((off_t) rip->i_size == start)
		return OK;

	if (start > (off_t) rip->i_size) {
		/* Grow: allocate clusters; the gap reads as zero (ValidData). */
		if ((r = extend_file(rip, (uint64_t) start)) != OK)
			return r;
		rip->i_size = (uint64_t) start;
		/* ValidDataLength stays put, so the gap reads back zero. */
	} else {
		/* Shrink. */
		need = ((uint64_t) start + pmp->pm_bytes_per_clus - 1) >>
		    pmp->pm_clus_byte_shift;
		have = chain_clusters(rip->i_start,
		    IS_CONTIG(rip) ? 1 : 0, rip->i_size);

		if (need == 0) {
			if (rip->i_start >= EXFAT_CLUST_FIRST &&
			    (r = free_chain(rip->i_start, IS_CONTIG(rip) ? 1 : 0,
			    have)) != OK)
				return r;
			rip->i_start = 0;
			rip->i_secflags = 0;
		} else if (need < have) {
			uint32_t keep, next;

			if (IS_CONTIG(rip)) {
				/* Free the contiguous tail. */
				if ((r = free_chain(
				    (uint32_t)(rip->i_start + need), 1,
				    have - need)) != OK)
					return r;
			} else {
				if ((r = chain_nth(rip->i_start, 0, need - 1,
				    &keep)) != OK)
					return r;
				if ((r = fat_get(keep, &next)) != OK)
					return r;
				if ((r = fat_set(keep, EXFAT_CLUST_EOF)) != OK)
					return r;
				if (next >= EXFAT_CLUST_FIRST &&
				    next <= pmp->pm_max_cluster &&
				    (r = free_chain(next, 0, have - need)) != OK)
					return r;
			}
		}
		rip->i_size = (uint64_t) start;
		if (rip->i_valid > rip->i_size)
			rip->i_valid = rip->i_size;
	}

	rip->i_mtime = exfat_now();
	return update_direntry(rip);
}

/*===========================================================================*
 *				fs_utime				     *
 *===========================================================================*/
int fs_utime(ino_t ino_nr, struct timespec *atime, struct timespec *mtime)
{
	struct inode *rip;

	(void) atime;

	if (pmp->pm_rdonly)
		return EROFS;
	if ((rip = find_inode(ino_nr)) == NULL)
		return EINVAL;

	if (mtime->tv_nsec == UTIME_NOW)
		rip->i_mtime = exfat_now();
	else if (mtime->tv_nsec != UTIME_OMIT)
		rip->i_mtime = mtime->tv_sec;

	return update_direntry(rip);
}

/*===========================================================================*
 *				fs_chmod				     *
 *===========================================================================*/
int fs_chmod(ino_t ino_nr, mode_t *mode)
{
/* exFAT can only represent a "read-only" bit, like FAT. */
	struct inode *rip;
	int r;

	if (pmp->pm_rdonly)
		return EROFS;
	if ((rip = find_inode(ino_nr)) == NULL)
		return EINVAL;

	if (*mode & S_IWUSR)
		rip->i_attrs &= ~EXFAT_ATTR_READONLY;
	else
		rip->i_attrs |= EXFAT_ATTR_READONLY;

	node_to_mode(rip);
	*mode = rip->i_mode;

	if ((r = update_direntry(rip)) != OK)
		return r;
	return OK;
}

/*===========================================================================*
 *				set_volume_dirty			     *
 *===========================================================================*/
void set_volume_dirty(int dirty)
{
/* Set or clear the VolumeDirty flag in the boot sector's VolumeFlags. */
	struct buf *bp;
	uint16_t flags;

	if (pmp == NULL || pmp->pm_rdonly)
		return;
	if ((bp = get_block(pmp->pm_dev, 0, NORMAL)) == NULL)
		return;

	flags = ex_get16((uint8_t *) b_data(bp) + 106);
	if (dirty)
		flags |= EXFAT_FLAG_VOLUME_DIRTY;
	else
		flags &= ~EXFAT_FLAG_VOLUME_DIRTY;
	ex_put16((uint8_t *) b_data(bp) + 106, flags);
	lmfs_markdirty(bp);
	put_block(bp);
}
