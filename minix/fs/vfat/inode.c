/* In-core node cache for the vfat server.
 *
 * A FAT file/dir has no inode number on disk; it is identified by the location
 * of its directory entry, (dirclust, diroffset).  We synthesize a stable inode
 * number from that location so that getdents, lookup and stat all agree.  The
 * root directory has no parent entry and uses the reserved number ROOT_INODE.
 */
#include "fs.h"
#include <sys/stat.h>

/*===========================================================================*
 *				make_ino				     *
 *===========================================================================*/
ino_t make_ino(unsigned long dirclust, unsigned long diroffset)
{
/* Deterministically map a directory-entry location to an inode number.
 * +2 keeps us clear of 0 ("no inode") and ROOT_INODE (1).
 */
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
/* Return the in-core inode with the given number, or NULL if not cached. */
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
/* Find a cached inode and add a reference. */
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
/* Return a free slot in the inode table, or NULL if the table is full. */
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
/* Derive a Unix st_mode from the FAT attribute byte and the mount masks. */
	mode_t perm;

	if (rip->i_attrs & ATTR_DIRECTORY) {
		perm = pmp->pm_dirmask;
		rip->i_mode = S_IFDIR | perm;
	} else {
		perm = pmp->pm_mask;
		rip->i_mode = S_IFREG | perm;
	}

	/* A read-only FAT entry loses all write permission bits. */
	if (rip->i_attrs & ATTR_READONLY)
		rip->i_mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
}

/*===========================================================================*
 *				fill_from_dirent			     *
 *===========================================================================*/
static void fill_from_dirent(struct inode *rip, const struct direntry *dp)
{
/* Populate an inode from its on-disk directory entry. */
	unsigned long start;
	struct timespec ts;

	start = getushort(dp->deStartCluster);
	if (FAT32(pmp))
		start |= (unsigned long) getushort(dp->deHighClust) << 16;

	rip->i_start = start;
	rip->i_attrs = dp->deAttributes;
	rip->i_size = (dp->deAttributes & ATTR_DIRECTORY) ? 0 :
	    getulong(dp->deFileSize);

	dos2unixtime(getushort(dp->deMDate), getushort(dp->deMTime), &ts);
	rip->i_mtime = ts.tv_sec;

	node_to_mode(rip);
	fc_init(rip);
}

/*===========================================================================*
 *				enter_inode				     *
 *===========================================================================*/
struct inode *enter_inode(unsigned long dirclust, unsigned long diroffset,
	const struct direntry *dp)
{
/* Return the in-core inode for the file whose directory entry lives at
 * (dirclust, diroffset), loading it from the entry dp if not already cached.
 * Adds a reference.
 */
	struct inode *rip;
	ino_t ino_nr;

	ino_nr = make_ino(dirclust, diroffset);

	if ((rip = get_inode(ino_nr)) != NULL)
		return rip;

	if ((rip = alloc_inode()) == NULL)
		return NULL;

	memset(rip, 0, sizeof(*rip));
	rip->i_num = ino_nr;
	rip->i_count = 1;
	rip->i_dev = pmp->pm_dev;
	rip->i_dirclust = dirclust;
	rip->i_diroffset = diroffset;
	rip->i_root = FALSE;
	fill_from_dirent(rip, dp);

	return rip;
}

/*===========================================================================*
 *				get_root_inode				     *
 *===========================================================================*/
struct inode *get_root_inode(void)
{
/* Construct (or fetch) the root directory inode. */
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
	rip->i_attrs = ATTR_DIRECTORY;
	rip->i_size = 0;

	/*
	 * For FAT32 the root is a normal cluster chain starting at
	 * pm_rootdirblk (a cluster number).  For FAT12/16 it is a fixed area;
	 * we represent its start cluster as MSDOSFSROOT (0) and special-case
	 * directory traversal in bmap()/getdents.
	 */
	rip->i_start = FAT32(pmp) ? pmp->pm_rootdirblk : MSDOSFSROOT;
	rip->i_dirclust = MSDOSFSROOT;
	rip->i_diroffset = 0;
	rip->i_mtime = 0;

	node_to_mode(rip);
	fc_init(rip);

	return rip;
}

/*===========================================================================*
 *				fs_putnode				     *
 *===========================================================================*/
int fs_putnode(ino_t ino_nr, unsigned int count)
{
/* Drop 'count' references to an inode. */
	struct inode *rip;

	if ((rip = find_inode(ino_nr)) == NULL) {
		printf("vfat: putnode of unknown inode %llu\n",
		    (unsigned long long) ino_nr);
		return EINVAL;
	}

	if (count > (unsigned int) rip->i_count)
		panic("vfat: putnode count %u > refcount %d", count,
		    rip->i_count);

	rip->i_count -= count;

	return OK;
}
