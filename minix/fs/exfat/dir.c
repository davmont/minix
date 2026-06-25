/* Directory handling for the exFAT server: entry-set iteration, root metadata
 * discovery (allocation bitmap + up-case table), lookup and getdents.
 *
 * An exFAT file or directory is described by a "directory entry set": a primary
 * File entry (0x85) followed by a Stream Extension entry (0xC0) and one or more
 * File Name entries (0xC1).  exFAT stores no "." or ".." entries, so those are
 * synthesized here.
 */
#include "fs.h"
#include <sys/stat.h>
#include <sys/dirent.h>

/*===========================================================================*
 *				read_dentry				     *
 *===========================================================================*/
static int read_dentry(struct inode *dir, uint64_t off,
	struct exfat_dentry *out)
{
/* Read the 32-byte directory entry at byte offset 'off' within directory
 * 'dir'.  An entry never crosses a sector boundary.  Returns ENOENT past the
 * end of the directory's cluster chain. */
	uint64_t frcn = off >> pmp->pm_clus_byte_shift;
	unsigned cloff = (unsigned)(off & (pmp->pm_bytes_per_clus - 1));
	uint64_t sec;
	unsigned secoff;
	struct buf *bp;
	int r;

	if ((r = bmap(dir, frcn, &sec)) != OK)
		return r;
	sec += cloff >> pmp->pm_sec_shift;
	secoff = (unsigned)(cloff & (pmp->pm_bytes_per_sec - 1));

	if ((bp = get_block(pmp->pm_dev, sec, NORMAL)) == NULL)
		return EIO;
	memcpy(out, b_data(bp) + secoff, EXFAT_DENTRY_SIZE);
	put_block(bp);

	return OK;
}

/*===========================================================================*
 *				read_fileset				     *
 *===========================================================================*/
static int read_fileset(struct inode *dir, uint64_t off,
	struct exfat_file_entry *fe, struct exfat_stream_entry *se,
	uint16_t *name, int *namelen)
{
/* Read the entry set whose File entry is at byte offset 'off'.  Fills in the
 * File and Stream entries and assembles the (UTF-16) name.  Returns OK, or an
 * error if the set is malformed/truncated. */
	struct exfat_dentry de;
	int sc, i, nl = 0, got_stream = 0;
	int r;

	if ((r = read_dentry(dir, off, (struct exfat_dentry *) fe)) != OK)
		return r;
	if (fe->type != EXFAT_ENTRY_FILE)
		return EINVAL;

	sc = fe->secondary_count;
	for (i = 1; i <= sc; i++) {
		if ((r = read_dentry(dir, off + (uint64_t) i *
		    EXFAT_DENTRY_SIZE, &de)) != OK)
			return r;

		if (de.type == EXFAT_ENTRY_STREAM) {
			memcpy(se, &de, EXFAT_DENTRY_SIZE);
			got_stream = 1;
		} else if (de.type == EXFAT_ENTRY_NAME) {
			const struct exfat_name_entry *ne =
			    (const struct exfat_name_entry *) &de;
			int k;

			for (k = 0; k < EXFAT_NAME_CHARS_PER_ENTRY &&
			    nl < EXFAT_NAME_MAX; k++)
				name[nl++] = ex_get16(ne->name + k * 2);
		}
	}

	if (!got_stream)
		return EINVAL;

	/* The stream entry's NameLength is authoritative. */
	if (se->name_length < nl)
		nl = se->name_length;
	*namelen = nl;

	return OK;
}

/*===========================================================================*
 *				set_entries				     *
 *===========================================================================*/
static unsigned set_entries(const struct exfat_file_entry *fe)
{
/* Number of 32-byte entries in the set headed by File entry fe. */
	return 1u + fe->secondary_count;
}

/*===========================================================================*
 *				scan_root_meta				     *
 *===========================================================================*/
int scan_root_meta(void)
{
/* Walk the root directory looking for the allocation-bitmap (0x81) and
 * up-case-table (0x82) entries, recording their locations in the mount.  These
 * benign metadata entries appear at the very start of the root directory. */
	struct inode *root;
	struct exfat_dentry de;
	uint64_t off;
	int r, found_bitmap = 0, found_upcase = 0;

	if ((root = find_inode(ROOT_INODE)) == NULL)
		return EINVAL;

	for (off = 0; ; off += EXFAT_DENTRY_SIZE) {
		if ((r = read_dentry(root, off, &de)) != OK) {
			if (r == ENOENT)
				break;		/* end of root chain */
			return r;
		}

		if (de.type == EXFAT_TYPE_END)
			break;
		if (!(de.type & EXFAT_TYPE_INUSE))
			continue;		/* deleted/unused */

		if (de.type == EXFAT_ENTRY_BITMAP && !found_bitmap) {
			const struct exfat_bitmap_entry *be =
			    (const struct exfat_bitmap_entry *) &de;

			/* flags bit0 selects the second (TexFAT) bitmap; we
			 * use the first. */
			if (!(be->flags & 0x01)) {
				pmp->pm_bitmap_cluster =
				    ex_get32(be->first_cluster);
				pmp->pm_bitmap_length =
				    ex_get64(be->data_length);
				found_bitmap = 1;
			}
		} else if (de.type == EXFAT_ENTRY_UPCASE && !found_upcase) {
			const struct exfat_upcase_entry *ue =
			    (const struct exfat_upcase_entry *) &de;

			pmp->pm_upcase_cluster = ex_get32(ue->first_cluster);
			pmp->pm_upcase_length = ex_get64(ue->data_length);
			found_upcase = 1;
		}

		if (found_bitmap && found_upcase)
			break;
	}

	if (!found_bitmap)
		return EINVAL;		/* a valid exFAT volume always has one */

	return OK;
}

/*===========================================================================*
 *				name_match				     *
 *===========================================================================*/
static int name_match(const uint16_t *a, int alen, const uint16_t *b, int blen)
{
/* Case-insensitive (up-cased) comparison of two UTF-16 names. */
	int i;

	if (alen != blen)
		return 0;
	for (i = 0; i < alen; i++)
		if (upcase_one(a[i]) != upcase_one(b[i]))
			return 0;
	return 1;
}

/*===========================================================================*
 *				fs_lookup				     *
 *===========================================================================*/
int fs_lookup(ino_t dir_nr, char *name, struct fsdriver_node *node,
	int *is_mountpt)
{
	struct inode *dirp, *rip;
	struct exfat_file_entry fe;
	struct exfat_stream_entry se;
	uint16_t target[EXFAT_NAME_MAX], dname[EXFAT_NAME_MAX];
	struct exfat_dentry de;
	uint64_t off;
	int tlen, dlen, r;

	if ((dirp = find_inode(dir_nr)) == NULL)
		return EINVAL;
	if (!(dirp->i_attrs & EXFAT_ATTR_DIRECTORY))
		return ENOTDIR;

	/* Synthetic "." and "..". */
	if (!strcmp(name, ".")) {
		rip = get_inode(dir_nr);
	} else if (!strcmp(name, "..")) {
		rip = get_parent_inode(dirp);
	} else {
		if ((tlen = utf8_to_utf16(name, target, EXFAT_NAME_MAX)) < 0)
			return ENAMETOOLONG;

		rip = NULL;
		for (off = 0; ; ) {
			if ((r = read_dentry(dirp, off, &de)) != OK) {
				if (r == ENOENT)
					break;
				return r;
			}
			if (de.type == EXFAT_TYPE_END)
				break;
			if (!(de.type & EXFAT_TYPE_INUSE) ||
			    de.type != EXFAT_ENTRY_FILE) {
				off += EXFAT_DENTRY_SIZE;
				continue;
			}

			if ((r = read_fileset(dirp, off, &fe, &se, dname,
			    &dlen)) != OK)
				return r;

			if (name_match(target, tlen, dname, dlen)) {
				rip = enter_inode(dirp, (uint32_t) off, &fe,
				    &se);
				break;
			}
			off += (uint64_t) set_entries(&fe) * EXFAT_DENTRY_SIZE;
		}
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
/* Directory stream cookie: 0 -> synthetic ".", 1 -> synthetic "..", >= 2 ->
 * byte offset (cookie - 2) of the next File entry to examine. */
	struct inode *dirp;
	struct exfat_file_entry fe;
	struct exfat_stream_entry se;
	uint16_t wname[EXFAT_NAME_MAX];
	char name[EXFAT_NAME_MAX * 4 + 1];
	struct exfat_dentry de;
	struct fsdriver_dentry fdd;
	off_t cookie;
	uint64_t off;
	ino_t child_ino;
	unsigned int type;
	int r, added, nl;
	static char buf[8192];

	if ((dirp = find_inode(ino_nr)) == NULL)
		return EINVAL;
	if (!(dirp->i_attrs & EXFAT_ATTR_DIRECTORY))
		return ENOTDIR;

	cookie = *pos;
	fsdriver_dentry_init(&fdd, data, bytes, buf, sizeof(buf));

	if (cookie == 0) {
		if ((added = fsdriver_dentry_add(&fdd, ino_nr, ".", 1,
		    DT_DIR)) < 0)
			return added;
		if (added == 0)
			goto out;
		cookie = 1;
	}
	if (cookie == 1) {
		ino_t parent = dirp->i_root ? ROOT_INODE : dirp->i_parent;

		if ((added = fsdriver_dentry_add(&fdd, parent, "..", 2,
		    DT_DIR)) < 0)
			return added;
		if (added == 0)
			goto out;
		cookie = 2;
	}

	off = (uint64_t)(cookie - 2);
	for (;;) {
		if ((r = read_dentry(dirp, off, &de)) != OK) {
			if (r == ENOENT)
				break;		/* end of chain */
			return r;
		}
		if (de.type == EXFAT_TYPE_END)
			break;
		if (!(de.type & EXFAT_TYPE_INUSE) ||
		    de.type != EXFAT_ENTRY_FILE) {
			off += EXFAT_DENTRY_SIZE;
			continue;
		}

		if ((r = read_fileset(dirp, off, &fe, &se, wname, &nl)) != OK)
			return r;
		if (utf16_to_utf8(wname, nl, name, sizeof(name)) < 0) {
			off += (uint64_t) set_entries(&fe) * EXFAT_DENTRY_SIZE;
			continue;	/* skip un-representable name */
		}

		child_ino = make_ino(dirp->i_start, (uint32_t) off);
		type = (ex_get16(fe.attributes) & EXFAT_ATTR_DIRECTORY) ?
		    DT_DIR : DT_REG;

		added = fsdriver_dentry_add(&fdd, child_ino, name,
		    strlen(name), type);
		if (added < 0)
			return added;
		if (added == 0) {
			/* Buffer full: resume at this same entry next time. */
			*pos = (off_t) off + 2;
			return fsdriver_dentry_finish(&fdd);
		}

		off += (uint64_t) set_entries(&fe) * EXFAT_DENTRY_SIZE;
	}

	cookie = (off_t) off + 2;

out:
	*pos = cookie;
	return fsdriver_dentry_finish(&fdd);
}
