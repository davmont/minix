/* In-core node cache for the exFAT server.
 *
 * An exFAT file/dir has no inode number on disk; it is identified by the
 * location of its File directory entry: the first cluster of the containing
 * directory plus the byte offset of the entry within that directory.  We
 * synthesize a stable inode number from that location so that getdents, lookup
 * and stat all agree.  The root directory uses the reserved number ROOT_INODE.
 */
#include "fs.h"
#include <sys/stat.h>

/*===========================================================================*
 *				make_ino				     *
 *===========================================================================*/
ino_t make_ino(uint32_t dirclust, uint32_t diroffset)
{
/* Deterministically map a directory-entry location to an inode number.
 * +2 keeps us clear of 0 ("no inode") and ROOT_INODE (1). */
	return (((ino_t) dirclust << 32) | (uint32_t) diroffset) + 2;
}

/*===========================================================================*
 *				init_inode_cache			     *
 *===========================================================================*/
void init_inode_cache(void)
{
	int i;

	for (i = 0; i < NR_INODES; i++)
		inode[i].i_count = 0;
}

/*===========================================================================*
 *				find_inode				     *
 *===========================================================================*/
struct inode *find_inode(ino_t ino_nr)
{
	int i;

	for (i = 0; i < NR_INODES; i++)
		if (inode[i].i_count > 0 && inode[i].i_num == ino_nr)
			return &inode[i];

	return NULL;
}

/*===========================================================================*
 *				get_inode				     *
 *===========================================================================*/
struct inode *get_inode(ino_t ino_nr)
{
	struct inode *rip;

	if ((rip = find_inode(ino_nr)) != NULL)
		rip->i_count++;

	return rip;
}

/*===========================================================================*
 *				put_inode				     *
 *===========================================================================*/
void put_inode(struct inode *rip)
{
	if (rip == NULL)
		return;

	assert(rip->i_count > 0);
	rip->i_count--;
}

/*===========================================================================*
 *				alloc_inode				     *
 *===========================================================================*/
static struct inode *alloc_inode(void)
{
	int i;

	for (i = 0; i < NR_INODES; i++)
		if (inode[i].i_count == 0)
			return &inode[i];

	return NULL;
}

/*===========================================================================*
 *				node_to_mode				     *
 *===========================================================================*/
void node_to_mode(struct inode *rip)
{
/* Derive a Unix st_mode from the exFAT attribute bits and the mount masks. */
	if (rip->i_attrs & EXFAT_ATTR_DIRECTORY)
		rip->i_mode = S_IFDIR | pmp->pm_dirmask;
	else
		rip->i_mode = S_IFREG | pmp->pm_mask;

	if (rip->i_attrs & EXFAT_ATTR_READONLY)
		rip->i_mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
}

/*===========================================================================*
 *				fill_from_entries			     *
 *===========================================================================*/
static void fill_from_entries(struct inode *rip,
	const struct exfat_file_entry *fe, const struct exfat_stream_entry *se)
{
/* Populate an inode from its File + Stream directory entries. */
	struct timespec ts;

	rip->i_attrs = ex_get16(fe->attributes);
	rip->i_secondary_count = fe->secondary_count;
	rip->i_secflags = se->flags;
	rip->i_start = ex_get32(se->first_cluster);
	rip->i_size = ex_get64(se->data_length);
	rip->i_valid = ex_get64(se->valid_data_length);

	exfat_to_timespec(ex_get32(fe->modify_time), fe->modify_10ms,
	    fe->modify_utc_offset, &ts);
	rip->i_mtime = ts.tv_sec;
	exfat_to_timespec(ex_get32(fe->create_time), fe->create_10ms,
	    fe->create_utc_offset, &ts);
	rip->i_ctime = ts.tv_sec;
	exfat_to_timespec(ex_get32(fe->access_time), 0,
	    fe->access_utc_offset, &ts);
	rip->i_atime = ts.tv_sec;

	node_to_mode(rip);
}

/*===========================================================================*
 *				enter_inode				     *
 *===========================================================================*/
struct inode *enter_inode(struct inode *dir, uint32_t diroffset,
	const struct exfat_file_entry *fe, const struct exfat_stream_entry *se)
{
/* Return the in-core inode for the file whose File entry lives at byte offset
 * 'diroffset' within directory 'dir', loading it from fe/se if not already
 * cached.  Adds a reference.  The parent identity is recorded so ".." can be
 * resolved later. */
	struct inode *rip;
	ino_t ino_nr;

	ino_nr = make_ino(dir->i_start, diroffset);

	if ((rip = get_inode(ino_nr)) != NULL)
		return rip;

	if ((rip = alloc_inode()) == NULL)
		return NULL;

	memset(rip, 0, sizeof(*rip));
	rip->i_num = ino_nr;
	rip->i_count = 1;
	rip->i_dev = pmp->pm_dev;
	rip->i_dirclust = dir->i_start;
	rip->i_diroffset = diroffset;
	rip->i_parent = dir->i_num;
	rip->i_parent_secflags = dir->i_secflags;
	rip->i_root = FALSE;
	fill_from_entries(rip, fe, se);

	return rip;
}

/*===========================================================================*
 *				get_parent_inode			     *
 *===========================================================================*/
struct inode *get_parent_inode(struct inode *rip)
{
/* Return (a referenced) inode for rip's parent directory, used to resolve
 * "..".  exFAT stores no ".." entry, so we rebuild the parent from the
 * identity recorded when rip was entered: its parent inode number, the
 * parent's first cluster (== rip->i_dirclust) and the parent's stream flags.
 * If the parent is still cached we return that (with full metadata); otherwise
 * we synthesize a minimal directory inode, which carries everything needed to
 * traverse and stat it. */
	struct inode *par;

	if (rip->i_root)
		return get_inode(ROOT_INODE);

	if ((par = get_inode(rip->i_parent)) != NULL)
		return par;

	if (rip->i_parent == ROOT_INODE)
		return get_root_inode();

	if ((par = alloc_inode()) == NULL)
		return NULL;

	memset(par, 0, sizeof(*par));
	par->i_num = rip->i_parent;
	par->i_count = 1;
	par->i_dev = pmp->pm_dev;
	par->i_attrs = EXFAT_ATTR_DIRECTORY;
	par->i_secflags = rip->i_parent_secflags;
	par->i_start = rip->i_dirclust;
	par->i_dirclust = rip->i_dirclust;	/* unknown grandparent; best effort */
	par->i_diroffset = 0;
	par->i_parent = ROOT_INODE;		/* conservative */
	par->i_root = FALSE;
	node_to_mode(par);

	return par;
}

/*===========================================================================*
 *				get_root_inode				     *
 *===========================================================================*/
struct inode *get_root_inode(void)
{
/* Construct (or fetch) the root directory inode.  The root is a normal
 * FAT-chained cluster chain starting at pm_root_cluster; its size is not
 * recorded anywhere, so directory iteration stops at the end-of-directory
 * marker or the end of the chain. */
	struct inode *rip;

	if ((rip = get_inode(ROOT_INODE)) != NULL)
		return rip;

	if ((rip = alloc_inode()) == NULL)
		return NULL;

	memset(rip, 0, sizeof(*rip));
	rip->i_num = ROOT_INODE;
	rip->i_count = 1;
	rip->i_dev = pmp->pm_dev;
	rip->i_root = TRUE;
	rip->i_attrs = EXFAT_ATTR_DIRECTORY;
	rip->i_secflags = 0;			/* root is always FAT-chained */
	rip->i_start = pmp->pm_root_cluster;
	rip->i_size = 0;
	rip->i_dirclust = pmp->pm_root_cluster;
	rip->i_diroffset = 0;
	rip->i_mtime = rip->i_ctime = rip->i_atime = 0;

	node_to_mode(rip);

	return rip;
}

/*===========================================================================*
 *				fs_putnode				     *
 *===========================================================================*/
int fs_putnode(ino_t ino_nr, unsigned int count)
{
	struct inode *rip;

	if ((rip = find_inode(ino_nr)) == NULL) {
		printf("exfat: putnode of unknown inode %llu\n",
		    (unsigned long long) ino_nr);
		return EINVAL;
	}

	if (count > (unsigned int) rip->i_count)
		panic("exfat: putnode count %u > refcount %d", count,
		    rip->i_count);

	rip->i_count -= count;

	return OK;
}
