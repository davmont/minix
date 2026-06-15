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
