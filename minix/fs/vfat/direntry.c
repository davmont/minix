/* Directory scanning, lookup and getdents for the vfat server.
 *
 * Handles VFAT long filenames (LFN): a file's long name is carried by one or
 * more 0x0f-attribute slots stored, in reverse order, immediately before the
 * 8.3 short entry.
 */
#include "fs.h"
#include <dirent.h>
#include <sys/stat.h>

/* Iterator over the resolved entries of a directory. */
struct de_iter {
	struct inode *dirp;
	unsigned long off;		/* byte offset of next raw slot */
	/* LFN accumulation state. */
	int have_lfn;
	int lfn_len;
	int lfn_chksum;
	unsigned char lfn[WIN_MAXLEN + 1];
};

/*===========================================================================*
 *				read_dir_slot				     *
 *===========================================================================*/
static int read_dir_slot(struct inode *dirp, unsigned long off,
	struct direntry *de)
{
/* Copy the 32-byte directory slot at byte offset 'off' into 'de'.  Returns OK,
 * or ENOENT at the end of the directory.
 */
	unsigned long sec, cn, bn, secoff;
	struct buf *bp;

	if (dirp->i_root && !FAT32(pmp)) {
		/* FAT12/16 fixed-size root directory. */
		if (off >= (unsigned long) pmp->pm_RootDirEnts * DIR_ENTRY_SIZE)
			return ENOENT;
		sec = pmp->pm_rootdirblk + off / pmp->pm_BytesPerSec;
	} else {
		unsigned long frcn = off / pmp->pm_bpcluster;
		int r;

		if ((r = bmap(dirp, frcn, &bn, &cn, NULL)) != OK)
			return r;
		if (bn == 0)
			return ENOENT;	/* past end of the cluster chain */
		sec = bn + (off % pmp->pm_bpcluster) / pmp->pm_BytesPerSec;
	}

	secoff = off % pmp->pm_BytesPerSec;
	if ((bp = get_block(pmp->pm_dev, sec, NORMAL)) == NULL)
		return EIO;
	memcpy(de, b_data(bp) + secoff, DIR_ENTRY_SIZE);
	put_block(bp);

	return OK;
}

/*===========================================================================*
 *				de_iter_init				     *
 *===========================================================================*/
static void de_iter_init(struct de_iter *it, struct inode *dirp,
	unsigned long off)
{
	it->dirp = dirp;
	it->off = off;
	it->have_lfn = FALSE;
	it->lfn_len = 0;
	it->lfn_chksum = -1;
}

/*===========================================================================*
 *				de_next					     *
 *===========================================================================*/
static int de_next(struct de_iter *it, char *name, size_t namesz,
	struct direntry *short_out, unsigned long *short_off)
{
/* Produce the next real directory entry.  Returns 1 and fills name/short_out/
 * short_off on success, 0 at the end of the directory, or a negative errno.
 * LFN slots are consumed and assembled internally.
 */
	struct direntry de;
	int r, cnt;

	for (;;) {
		if ((r = read_dir_slot(it->dirp, it->off, &de)) != OK) {
			if (r == ENOENT)
				return 0;	/* end of directory */
			return -r;
		}
		it->off += DIR_ENTRY_SIZE;

		if (de.deName[0] == SLOT_EMPTY)
			return 0;		/* no more entries */

		if (de.deName[0] == SLOT_DELETED) {
			it->have_lfn = FALSE;
			continue;
		}

		if (de.deAttributes == ATTR_WIN95) {
			/* Long-name slot. */
			const struct winentry *wep = (const struct winentry *) &de;
			int seq = wep->weCnt & WIN_CNT;
			int pos, got;

			if (seq < 1 || seq * WIN_CHARS > WIN_MAXLEN) {
				it->have_lfn = FALSE;
				continue;
			}
			if (wep->weCnt & WIN_LAST) {
				it->have_lfn = TRUE;
				it->lfn_chksum = wep->weChksum;
				it->lfn_len = 0;
				memset(it->lfn, 0, sizeof(it->lfn));
			}
			if (!it->have_lfn)
				continue;

			pos = (seq - 1) * WIN_CHARS;
			got = win2unixfn(wep, &it->lfn[pos],
			    sizeof(it->lfn) - 1 - pos, it->lfn_chksum);
			if (got < 0) {
				it->have_lfn = FALSE;	/* checksum mismatch */
				continue;
			}
			if (pos + got > it->lfn_len)
				it->lfn_len = pos + got;
			continue;
		}

		if (de.deAttributes & ATTR_VOLUME) {
			it->have_lfn = FALSE;
			continue;		/* skip volume label */
		}

		/* A real 8.3 entry. */
		if (it->have_lfn &&
		    winchksum(de.deName) == (uint8_t) it->lfn_chksum &&
		    it->lfn_len > 0) {
			it->lfn[it->lfn_len] = '\0';
			(void) strlcpy(name, (char *) it->lfn, namesz);
		} else {
			cnt = dos2unixfn(de.deName, (unsigned char *) name, 1);
			(void) cnt;
		}
		it->have_lfn = FALSE;

		*short_out = de;
		*short_off = it->off - DIR_ENTRY_SIZE;
		return 1;
	}
}

/*===========================================================================*
 *				casematch				     *
 *===========================================================================*/
static int casematch(const char *a, const char *b)
{
/* Case-insensitive ASCII string compare; returns TRUE on match. */
	while (*a && *b) {
		int ca = (unsigned char) *a;
		int cb = (unsigned char) *b;

		if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
		if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
		if (ca != cb)
			return FALSE;
		a++;
		b++;
	}
	return *a == '\0' && *b == '\0';
}

/*===========================================================================*
 *				fs_lookup				     *
 *===========================================================================*/
int fs_lookup(ino_t dir_nr, char *name, struct fsdriver_node *node,
	int *is_mountpt)
{
	struct inode *dirp, *rip;
	struct de_iter it;
	struct direntry de;
	char entname[WIN_MAXLEN + 1];
	unsigned long short_off;
	int r;

	if ((dirp = find_inode(dir_nr)) == NULL)
		return EINVAL;

	if (!(dirp->i_attrs & ATTR_DIRECTORY))
		return ENOTDIR;

	/* "." resolves to the directory itself. */
	if (!strcmp(name, ".")) {
		rip = get_inode(dir_nr);
	} else if (!strcmp(name, "..") && dirp->i_root) {
		/* ".." in our root stays at the root; VFS handles the real
		 * parent across the mount point. */
		rip = get_inode(dir_nr);
	} else {
		de_iter_init(&it, dirp, 0);
		rip = NULL;
		while ((r = de_next(&it, entname, sizeof(entname), &de,
		    &short_off)) > 0) {
			if (casematch(entname, name)) {
				rip = enter_inode(dirp->i_start, short_off, &de);
				break;
			}
		}
		if (r < 0)
			return -r;
		if (rip == NULL)
			return ENOENT;
	}

	if (rip == NULL)
		return ENOENT;

	node->fn_ino_nr = rip->i_num;
	node->fn_mode = rip->i_mode;
	node->fn_size = rip->i_size;
	node->fn_uid = pmp->pm_uid;
	node->fn_gid = pmp->pm_gid;
	node->fn_dev = NO_DEV;

	*is_mountpt = rip->i_mountpoint;

	return OK;
}

/*===========================================================================*
 *				fs_getdents				     *
 *===========================================================================*/
ssize_t fs_getdents(ino_t ino_nr, struct fsdriver_data *data, size_t bytes,
	off_t *pos)
{
/* Directory stream cookie encoding:
 *   0 -> synthetic "."   1 -> synthetic ".."   >=2 -> on-disk entry at byte
 *   offset (cookie - 2).  On-disk "." and ".." entries are skipped (we always
 *   synthesize them).
 */
	struct inode *dirp;
	struct de_iter it;
	struct direntry de;
	char entname[WIN_MAXLEN + 1];
	struct fsdriver_dentry fdd;
	unsigned long short_off;
	off_t cookie;
	ino_t child_ino;
	unsigned int type;
	int r, added;
	static char buf[8192];

	if ((dirp = find_inode(ino_nr)) == NULL)
		return EINVAL;
	if (!(dirp->i_attrs & ATTR_DIRECTORY))
		return ENOTDIR;

	cookie = *pos;
	fsdriver_dentry_init(&fdd, data, bytes, buf, sizeof(buf));

	/* Synthetic "." */
	if (cookie == 0) {
		if ((added = fsdriver_dentry_add(&fdd, ino_nr, ".", 1,
		    DT_DIR)) < 0)
			return added;
		if (added == 0)
			goto out;
		cookie = 1;
	}
	/* Synthetic ".." (parent ino is approximate for non-root dirs). */
	if (cookie == 1) {
		ino_t parent = dirp->i_root ? ROOT_INODE : ino_nr;

		if ((added = fsdriver_dentry_add(&fdd, parent, "..", 2,
		    DT_DIR)) < 0)
			return added;
		if (added == 0)
			goto out;
		cookie = 2;
	}

	de_iter_init(&it, dirp, (unsigned long) (cookie - 2));
	while ((r = de_next(&it, entname, sizeof(entname), &de,
	    &short_off)) > 0) {
		/* Skip the on-disk dot entries; we synthesize them. */
		if (!strcmp(entname, ".") || !strcmp(entname, ".."))
			continue;

		child_ino = make_ino(dirp->i_start, short_off);
		type = (de.deAttributes & ATTR_DIRECTORY) ? DT_DIR : DT_REG;

		added = fsdriver_dentry_add(&fdd, child_ino, entname,
		    strlen(entname), type);
		if (added < 0)
			return added;
		if (added == 0) {
			/* Buffer full: resume past this entry's short slot. */
			cookie = (off_t) it.off - DIR_ENTRY_SIZE + 2;
			*pos = cookie;
			return fsdriver_dentry_finish(&fdd);
		}
	}
	if (r < 0)
		return -r;

	/* Reached the end of the directory. */
	cookie = (off_t) it.off + 2;

out:
	*pos = cookie;
	return fsdriver_dentry_finish(&fdd);
}

/*===========================================================================*
 *				find_entry				     *
 *===========================================================================*/
static int find_entry(struct inode *dirp, const char *name,
	struct direntry *de_out, unsigned long *short_off)
{
/* Scan dirp for an entry matching 'name' (case-insensitive).  Returns 1 and
 * fills de_out/short_off on a match, 0 if not found, or a negative errno.
 */
	struct de_iter it;
	struct direntry de;
	char entname[WIN_MAXLEN + 1];
	int r;

	de_iter_init(&it, dirp, 0);
	while ((r = de_next(&it, entname, sizeof(entname), &de, short_off)) > 0)
		if (casematch(entname, name)) {
			*de_out = de;
			return 1;
		}

	return (r < 0) ? r : 0;
}

/*===========================================================================*
 *				write_dir_slot				     *
 *===========================================================================*/
static int write_dir_slot(struct inode *dirp, unsigned long off,
	const void *buf)
{
/* Write a 32-byte directory slot at byte offset 'off' within dirp. */
	struct buf *bp;
	unsigned long sec;

	sec = entry_sector(dirp->i_start, off);
	if (sec == 0)
		return EIO;
	if ((bp = get_block(pmp->pm_dev, sec, NORMAL)) == NULL)
		return EIO;
	memcpy(b_data(bp) + off % pmp->pm_BytesPerSec, buf, DIR_ENTRY_SIZE);
	lmfs_markdirty(bp);
	put_block(bp);

	return OK;
}

/*===========================================================================*
 *				short_name_exists			     *
 *===========================================================================*/
static int short_name_exists(struct inode *dirp, const unsigned char name11[11])
{
/* Return TRUE if a live 8.3 entry with the given raw name already exists. */
	struct direntry de;
	unsigned long off = 0;
	int r;

	for (;;) {
		if ((r = read_dir_slot(dirp, off, &de)) != OK)
			return FALSE;	/* ENOENT == end of directory */
		off += DIR_ENTRY_SIZE;
		if (de.deName[0] == SLOT_EMPTY)
			return FALSE;
		if (de.deName[0] == SLOT_DELETED ||
		    de.deAttributes == ATTR_WIN95)
			continue;
		if (!memcmp(de.deName, name11, 11))
			return TRUE;
	}
}

/*===========================================================================*
 *				make_short_name				     *
 *===========================================================================*/
static void make_short_name(struct inode *dirp, const char *name,
	unsigned char out11[11])
{
/* Derive a unique uppercase 8.3 short name (BASE~N.EXT) for 'name'. */
	const char *dot;
	char base[8], ext[3];
	int bl = 0, el = 0, i, n;
	unsigned char c;

	dot = strrchr(name, '.');
	for (i = 0; name[i] != '\0' && (dot == NULL || &name[i] < dot) &&
	    bl < 6; i++) {
		c = (unsigned char) name[i];
		if (c == ' ' || c == '.')
			continue;
		if (c >= 'a' && c <= 'z')
			c -= 'a' - 'A';
		else if (c >= 0x80 || c < '0')
			c = '_';
		base[bl++] = c;
	}
	if (dot != NULL)
		for (i = 1; dot[i] != '\0' && el < 3; i++) {
			c = (unsigned char) dot[i];
			if (c >= 'a' && c <= 'z')
				c -= 'a' - 'A';
			else if (c >= 0x80 || c < '0')
				c = '_';
			ext[el++] = c;
		}

	for (n = 1; n < 1000000; n++) {
		char num[8];
		int nl, k, p = 0;

		nl = snprintf(num, sizeof(num), "~%d", n);
		memset(out11, ' ', 11);
		for (k = 0; k < bl && p < 8 - nl; k++)
			out11[p++] = base[k];
		for (k = 0; k < nl; k++)
			out11[p++] = num[k];
		for (k = 0; k < el; k++)
			out11[8 + k] = ext[k];
		if (!short_name_exists(dirp, out11))
			return;
	}
}

/*===========================================================================*
 *				put_wchar				     *
 *===========================================================================*/
static void put_wchar(struct winentry *wep, int k, unsigned int wc)
{
/* Store the k-th (0..12) UTF-16LE character of a long-name slot. */
	uint8_t *p;

	if (k < 5)
		p = &wep->wePart1[k * 2];
	else if (k < 11)
		p = &wep->wePart2[(k - 5) * 2];
	else
		p = &wep->wePart3[(k - 11) * 2];
	p[0] = wc & 0xff;
	p[1] = (wc >> 8) & 0xff;
}

/*===========================================================================*
 *				fill_winentry				     *
 *===========================================================================*/
static void fill_winentry(struct winentry *wep, const char *name, int chunklen,
	int seq, int last, int chksum)
{
	int k;

	memset(wep, 0, DIR_ENTRY_SIZE);
	wep->weCnt = seq | (last ? WIN_LAST : 0);
	wep->weAttributes = ATTR_WIN95;
	wep->weChksum = (uint8_t) chksum;

	for (k = 0; k < WIN_CHARS; k++) {
		unsigned int wc;

		if (k < chunklen)
			wc = (unsigned char) name[k];
		else if (k == chunklen)
			wc = 0x0000;	/* terminator */
		else
			wc = 0xffff;	/* padding */
		put_wchar(wep, k, wc);
	}
}

/*===========================================================================*
 *				find_free_slots				     *
 *===========================================================================*/
static int find_free_slots(struct inode *dirp, int need, unsigned long *off_out)
{
/* Find a run of 'need' consecutive free directory slots, extending the
 * directory by clusters when possible.  FAT12/16 root cannot grow.
 */
	struct direntry de;
	unsigned long off = 0, runstart = 0;
	int run = 0, r;

	for (;;) {
		r = read_dir_slot(dirp, off, &de);
		if (r == ENOENT) {
			if (dirp->i_root && !FAT32(pmp))
				return ENOSPC;	/* fixed-size root */
			if ((r = extend_file(dirp, off + pmp->pm_bpcluster)) != OK)
				return r;
			continue;		/* retry: new cluster is zeroed */
		}
		if (r != OK)
			return r;

		if (de.deName[0] == SLOT_EMPTY ||
		    de.deName[0] == SLOT_DELETED) {
			if (run == 0)
				runstart = off;
			if (++run >= need) {
				*off_out = runstart;
				return OK;
			}
		} else
			run = 0;
		off += DIR_ENTRY_SIZE;
	}
}

/*===========================================================================*
 *				createde				     *
 *===========================================================================*/
static int createde(struct inode *dirp, const char *name, uint8_t attrs,
	unsigned long start, uint32_t size, unsigned long *short_off_out)
{
/* Create a directory entry (long-name slots + 8.3 entry) for 'name'. */
	unsigned char name11[11];
	struct direntry de;
	struct winentry we;
	uint16_t dosdate, dostime;
	unsigned long off;
	int namelen, nslots, p, chksum, r;

	namelen = (int) strlen(name);
	if (namelen == 0 || namelen > WIN_MAXLEN)
		return EINVAL;

	make_short_name(dirp, name, name11);
	chksum = winchksum(name11);
	nslots = (namelen + WIN_CHARS - 1) / WIN_CHARS;

	if ((r = find_free_slots(dirp, nslots + 1, &off)) != OK)
		return r;

	for (p = 0; p < nslots; p++) {
		int seq = nslots - p;
		int cstart = (seq - 1) * WIN_CHARS;
		int clen = namelen - cstart;

		if (clen > WIN_CHARS)
			clen = WIN_CHARS;
		fill_winentry(&we, name + cstart, clen, seq, p == 0, chksum);
		if ((r = write_dir_slot(dirp, off + p * DIR_ENTRY_SIZE,
		    &we)) != OK)
			return r;
	}

	memset(&de, 0, sizeof(de));
	memcpy(de.deName, name11, 11);
	de.deAttributes = attrs;
	putushort(de.deStartCluster, start & 0xffff);
	if (FAT32(pmp))
		putushort(de.deHighClust, (start >> 16) & 0xffff);
	putulong(de.deFileSize, size);
	unix2dostime(vfat_now(), &dosdate, &dostime);
	putushort(de.deMDate, dosdate);
	putushort(de.deMTime, dostime);
	putushort(de.deCDate, dosdate);
	putushort(de.deCTime, dostime);
	putushort(de.deADate, dosdate);

	if ((r = write_dir_slot(dirp, off + nslots * DIR_ENTRY_SIZE,
	    &de)) != OK)
		return r;

	*short_off_out = off + nslots * DIR_ENTRY_SIZE;
	return OK;
}

/*===========================================================================*
 *				removede				     *
 *===========================================================================*/
static int removede(struct inode *dirp, unsigned long short_off)
{
/* Mark the 8.3 entry at short_off, and the long-name slots preceding it,
 * as deleted.
 */
	struct direntry de;
	unsigned long off = short_off;
	int r;

	if ((r = read_dir_slot(dirp, off, &de)) != OK)
		return r;
	de.deName[0] = SLOT_DELETED;
	if ((r = write_dir_slot(dirp, off, &de)) != OK)
		return r;

	while (off >= DIR_ENTRY_SIZE) {
		off -= DIR_ENTRY_SIZE;
		if ((r = read_dir_slot(dirp, off, &de)) != OK)
			break;
		if (de.deAttributes != ATTR_WIN95 ||
		    de.deName[0] == SLOT_DELETED)
			break;
		de.deName[0] = SLOT_DELETED;
		if ((r = write_dir_slot(dirp, off, &de)) != OK)
			return r;
	}

	return OK;
}

/*===========================================================================*
 *				node_from_dirent			     *
 *===========================================================================*/
static int node_from_dirent(struct inode *dirp, unsigned long short_off,
	struct fsdriver_node *node)
{
/* After creating an entry, build its inode and fill the reply node. */
	struct inode *rip;
	struct direntry de;
	int r;

	if ((r = read_dir_slot(dirp, short_off, &de)) != OK)
		return r;
	if ((rip = enter_inode(dirp->i_start, short_off, &de)) == NULL)
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
 *				fs_create				     *
 *===========================================================================*/
int fs_create(ino_t dir_nr, char *name, mode_t mode, uid_t uid, gid_t gid,
	struct fsdriver_node *node)
{
	struct inode *dirp;
	struct direntry de;
	unsigned long soff;
	int r;

	(void) mode; (void) uid; (void) gid;

	if (pmp->pm_rdonly)
		return EROFS;
	if ((dirp = find_inode(dir_nr)) == NULL)
		return EINVAL;
	if (!(dirp->i_attrs & ATTR_DIRECTORY))
		return ENOTDIR;

	if ((r = find_entry(dirp, name, &de, &soff)) < 0)
		return -r;
	if (r == 1)
		return EEXIST;

	if ((r = createde(dirp, name, ATTR_ARCHIVE, 0, 0, &soff)) != OK)
		return r;

	return node_from_dirent(dirp, soff, node);
}

/*===========================================================================*
 *				write_dot_entries			     *
 *===========================================================================*/
static int write_dot_entries(unsigned long cn, unsigned long parentcn)
{
/* Write the "." and ".." entries into the first sector of new directory
 * cluster cn.  parentcn is the parent's start cluster (0 for the root).
 */
	struct buf *bp;
	struct direntry *dep;
	uint16_t dosdate, dostime;
	unsigned long sec;

	sec = cntobn(pmp, cn);
	if ((bp = get_block(pmp->pm_dev, sec, NORMAL)) == NULL)
		return EIO;

	unix2dostime(vfat_now(), &dosdate, &dostime);

	dep = (struct direntry *) b_data(bp);
	memset(dep, 0, 2 * DIR_ENTRY_SIZE);

	memset(dep[0].deName, ' ', 11);
	dep[0].deName[0] = '.';
	dep[0].deAttributes = ATTR_DIRECTORY;
	putushort(dep[0].deStartCluster, cn & 0xffff);
	if (FAT32(pmp))
		putushort(dep[0].deHighClust, (cn >> 16) & 0xffff);
	putushort(dep[0].deMDate, dosdate);
	putushort(dep[0].deMTime, dostime);

	memset(dep[1].deName, ' ', 11);
	dep[1].deName[0] = '.';
	dep[1].deName[1] = '.';
	dep[1].deAttributes = ATTR_DIRECTORY;
	putushort(dep[1].deStartCluster, parentcn & 0xffff);
	if (FAT32(pmp))
		putushort(dep[1].deHighClust, (parentcn >> 16) & 0xffff);
	putushort(dep[1].deMDate, dosdate);
	putushort(dep[1].deMTime, dostime);

	lmfs_markdirty(bp);
	put_block(bp);

	return OK;
}

/*===========================================================================*
 *				fs_mkdir				     *
 *===========================================================================*/
int fs_mkdir(ino_t dir_nr, char *name, mode_t mode, uid_t uid, gid_t gid)
{
	struct inode *dirp;
	struct direntry de;
	unsigned long soff, newcn, parentcn;
	int r;

	(void) mode; (void) uid; (void) gid;

	if (pmp->pm_rdonly)
		return EROFS;
	if ((dirp = find_inode(dir_nr)) == NULL)
		return EINVAL;
	if (!(dirp->i_attrs & ATTR_DIRECTORY))
		return ENOTDIR;

	if ((r = find_entry(dirp, name, &de, &soff)) < 0)
		return -r;
	if (r == 1)
		return EEXIST;

	/* Allocate and initialize the new directory's first cluster. */
	if ((r = cluster_alloc(0, &newcn)) != OK)
		return r;
	if ((r = zero_cluster(newcn)) != OK)
		return r;

	parentcn = dirp->i_root ? 0 : dirp->i_start;
	if ((r = write_dot_entries(newcn, parentcn)) != OK)
		return r;

	if ((r = createde(dirp, name, ATTR_DIRECTORY, newcn, 0, &soff)) != OK) {
		(void) free_chain(newcn);
		return r;
	}

	return OK;
}

/*===========================================================================*
 *				dir_is_empty				     *
 *===========================================================================*/
static int dir_is_empty(unsigned long start)
{
/* Return TRUE if the directory whose first cluster is 'start' contains no
 * entries other than "." and "..".
 */
	struct inode tmp;
	struct de_iter it;
	struct direntry de;
	char entname[WIN_MAXLEN + 1];
	unsigned long soff;
	int r;

	memset(&tmp, 0, sizeof(tmp));
	tmp.i_start = start;
	tmp.i_attrs = ATTR_DIRECTORY;
	fc_init(&tmp);

	de_iter_init(&it, &tmp, 0);
	while ((r = de_next(&it, entname, sizeof(entname), &de, &soff)) > 0)
		if (strcmp(entname, ".") && strcmp(entname, ".."))
			return FALSE;

	return TRUE;
}

/*===========================================================================*
 *				fs_unlink				     *
 *===========================================================================*/
int fs_unlink(ino_t dir_nr, char *name, int call)
{
	struct inode *dirp;
	struct direntry de;
	unsigned long soff, start;
	int r, isdir;

	if (pmp->pm_rdonly)
		return EROFS;
	if ((dirp = find_inode(dir_nr)) == NULL)
		return EINVAL;
	if (!(dirp->i_attrs & ATTR_DIRECTORY))
		return ENOTDIR;

	if ((r = find_entry(dirp, name, &de, &soff)) < 0)
		return -r;
	if (r == 0)
		return ENOENT;

	isdir = (de.deAttributes & ATTR_DIRECTORY) ? 1 : 0;
	if (call == FSC_RMDIR && !isdir)
		return ENOTDIR;
	if (call == FSC_UNLINK && isdir)
		return EPERM;

	start = getushort(de.deStartCluster);
	if (FAT32(pmp))
		start |= (unsigned long) getushort(de.deHighClust) << 16;

	if (call == FSC_RMDIR) {
		if (!strcmp(name, ".") || !strcmp(name, ".."))
			return EINVAL;
		if (!dir_is_empty(start))
			return ENOTEMPTY;
	}

	if ((r = removede(dirp, soff)) != OK)
		return r;

	if (start >= CLUST_FIRST && start <= pmp->pm_maxcluster)
		if ((r = free_chain(start)) != OK)
			return r;

	return OK;
}

/*===========================================================================*
 *				update_dotdot				     *
 *===========================================================================*/
static int update_dotdot(unsigned long dir_start, unsigned long new_parent)
{
/* Point the ".." entry in the directory whose first cluster is dir_start at
 * new_parent (0 for the root).  ".." is the second slot of the first cluster.
 */
	struct buf *bp;
	struct direntry *dep;
	unsigned long sec;

	sec = cntobn(pmp, dir_start);
	if ((bp = get_block(pmp->pm_dev, sec, NORMAL)) == NULL)
		return EIO;

	dep = &((struct direntry *) b_data(bp))[1];	/* ".." */
	putushort(dep->deStartCluster, new_parent & 0xffff);
	if (FAT32(pmp))
		putushort(dep->deHighClust, (new_parent >> 16) & 0xffff);

	lmfs_markdirty(bp);
	put_block(bp);

	return OK;
}

/*===========================================================================*
 *				dirent_start				     *
 *===========================================================================*/
static unsigned long dirent_start(const struct direntry *de)
{
	unsigned long start = getushort(de->deStartCluster);

	if (FAT32(pmp))
		start |= (unsigned long) getushort(de->deHighClust) << 16;
	return start;
}

/*===========================================================================*
 *				fs_rename				     *
 *===========================================================================*/
int fs_rename(ino_t old_dir_nr, char *old_name, ino_t new_dir_nr,
	char *new_name)
{
	struct inode *old_dirp, *new_dirp, *rip;
	struct direntry old_de, new_de;
	unsigned long old_soff, new_soff, start, size;
	uint8_t attrs;
	int r, isdir;

	if (pmp->pm_rdonly)
		return EROFS;

	if ((old_dirp = find_inode(old_dir_nr)) == NULL ||
	    (new_dirp = find_inode(new_dir_nr)) == NULL)
		return EINVAL;
	if (!(old_dirp->i_attrs & ATTR_DIRECTORY) ||
	    !(new_dirp->i_attrs & ATTR_DIRECTORY))
		return ENOTDIR;

	/* Locate the source. */
	if ((r = find_entry(old_dirp, old_name, &old_de, &old_soff)) < 0)
		return -r;
	if (r == 0)
		return ENOENT;

	isdir = (old_de.deAttributes & ATTR_DIRECTORY) ? 1 : 0;
	start = dirent_start(&old_de);
	size = getulong(old_de.deFileSize);
	attrs = old_de.deAttributes;

	/* No-op rename onto the exact same entry. */
	if (old_dirp == new_dirp && !strcmp(old_name, new_name))
		return OK;

	/* Replace an existing target, if any. */
	if ((r = find_entry(new_dirp, new_name, &new_de, &new_soff)) < 0)
		return -r;
	if (r == 1) {
		int tisdir = (new_de.deAttributes & ATTR_DIRECTORY) ? 1 : 0;
		unsigned long tstart = dirent_start(&new_de);

		if (isdir != tisdir)
			return isdir ? ENOTDIR : EISDIR;
		if (tisdir && !dir_is_empty(tstart))
			return ENOTEMPTY;
		if ((r = removede(new_dirp, new_soff)) != OK)
			return r;
		if (tstart >= CLUST_FIRST && tstart <= pmp->pm_maxcluster)
			if ((r = free_chain(tstart)) != OK)
				return r;
	}

	/* Create the destination entry, then drop the source. */
	if ((r = createde(new_dirp, new_name, attrs, start, (uint32_t) size,
	    &new_soff)) != OK)
		return r;
	if ((r = removede(old_dirp, old_soff)) != OK)
		return r;

	/* A directory moved to a different parent needs its ".." fixed. */
	if (isdir && old_dirp != new_dirp && start >= CLUST_FIRST) {
		unsigned long np = new_dirp->i_root ? 0 : new_dirp->i_start;

		if ((r = update_dotdot(start, np)) != OK)
			return r;
	}

	/* Re-key a cached inode to its new directory-entry location. */
	rip = find_inode(make_ino(old_dirp->i_start, old_soff));
	if (rip != NULL) {
		rip->i_num = make_ino(new_dirp->i_start, new_soff);
		rip->i_dirclust = new_dirp->i_start;
		rip->i_diroffset = new_soff;
	}

	return OK;
}
