/* Mount / unmount for the exFAT server.
 *
 * The geometry is taken straight from the Main Boot Sector.  exFAT is
 * read-only for now, so the device is opened read-only and RES_RDONLY is
 * reported back to VFS.
 */
#include "fs.h"
#include <minix/vfsif.h>
#include <sys/stat.h>

/*===========================================================================*
 *				parse_boot				     *
 *===========================================================================*/
static int parse_boot(const struct exfat_boot *b)
{
/* Parse and validate the Main Boot Sector into the global mount (pmp). */
	unsigned secshift, clushift;

	if (memcmp(b->fs_name, EXFAT_FS_NAME, 8) != 0)
		return EINVAL;
	if (ex_get16(b->boot_signature) != EXFAT_BOOT_SIG)
		return EINVAL;

	secshift = b->bytes_per_sector_shift;
	clushift = b->sectors_per_cluster_shift;

	/* Sector size 512..4096; cluster size up to 32 MB. */
	if (secshift < 9 || secshift > 12)
		return EINVAL;
	if (secshift + clushift > 25)
		return EINVAL;

	memset(pmp, 0, sizeof(*pmp));

	pmp->pm_sec_shift = secshift;
	pmp->pm_bytes_per_sec = 1u << secshift;
	pmp->pm_clus_shift = clushift;
	pmp->pm_sec_per_clus = 1u << clushift;
	pmp->pm_clus_byte_shift = secshift + clushift;
	pmp->pm_bytes_per_clus = 1ul << pmp->pm_clus_byte_shift;

	pmp->pm_fat_off = ex_get32(b->fat_offset);
	pmp->pm_fat_length = ex_get32(b->fat_length);
	pmp->pm_cluster_heap_off = ex_get32(b->cluster_heap_offset);
	pmp->pm_cluster_count = ex_get32(b->cluster_count);
	pmp->pm_root_cluster = ex_get32(b->first_cluster_of_root);
	pmp->pm_num_fats = b->number_of_fats;

	pmp->pm_max_cluster = pmp->pm_cluster_count + 1;	/* clusters 2.. */

	if (pmp->pm_num_fats < 1 || pmp->pm_fat_length == 0 ||
	    pmp->pm_cluster_heap_off == 0 || pmp->pm_cluster_count == 0)
		return EINVAL;
	if (pmp->pm_root_cluster < EXFAT_CLUST_FIRST ||
	    pmp->pm_root_cluster > pmp->pm_max_cluster)
		return EINVAL;

	return OK;
}

/*===========================================================================*
 *				fs_mount				     *
 *===========================================================================*/
int fs_mount(dev_t dev, unsigned int flags, struct fsdriver_node *root_node,
	unsigned int *res_flags)
{
	static char bootbuf[EXFAT_MIN_SECTOR];
	struct buf *bp;
	struct inode *root;
	int r, readonly;

	readonly = (flags & REQ_RDONLY) ? 1 : 0;

	if (bdev_open(dev, readonly ? BDEV_R_BIT : (BDEV_R_BIT | BDEV_W_BIT))
	    != OK)
		return EINVAL;

	pmp = &exfat_mount;

	/* Read the boot sector at the provisional 512-byte block size into a
	 * local copy; all header fields live within the first 512 bytes. */
	lmfs_set_blocksize(EXFAT_MIN_SECTOR);
	if ((bp = get_block(dev, 0, NORMAL)) == NULL) {
		bdev_close(dev);
		return EINVAL;
	}
	memcpy(bootbuf, b_data(bp), sizeof(bootbuf));
	put_block(bp);

	if ((r = parse_boot((const struct exfat_boot *) bootbuf)) != OK) {
		bdev_close(dev);
		return r;
	}

	/* Switch the cache to the real sector size (also the lmfs block size,
	 * so a device sector number is an lmfs block number). */
	if (pmp->pm_bytes_per_sec != EXFAT_MIN_SECTOR)
		lmfs_set_blocksize(pmp->pm_bytes_per_sec);

	pmp->pm_dev = dev;
	pmp->pm_rdonly = readonly;
	pmp->pm_uid = 0;
	pmp->pm_gid = 0;
	pmp->pm_mask = DEFAULT_FMASK;
	pmp->pm_dirmask = DEFAULT_DMASK;
	pmp->pm_upcase = NULL;

	/* Build the root inode (needed to read directory entries). */
	if ((root = get_root_inode()) == NULL) {
		bdev_close(dev);
		return EINVAL;
	}

	/* Find the allocation bitmap and up-case table in the root directory. */
	if ((r = scan_root_meta()) != OK) {
		put_inode(root);
		bdev_close(dev);
		return r;
	}
	if ((r = upcase_init()) != OK) {
		put_inode(root);
		bdev_close(dev);
		return r;
	}
	if ((r = bitmap_count_free()) != OK) {
		upcase_free();
		put_inode(root);
		bdev_close(dev);
		return r;
	}

	lmfs_set_blockusage(pmp->pm_cluster_count,
	    pmp->pm_cluster_count - pmp->pm_free_clusters);

	/* Mark the volume dirty for the duration of a writable mount. */
	if (!readonly)
		set_volume_dirty(1);

	mounted = TRUE;

	root_node->fn_ino_nr = root->i_num;
	root_node->fn_mode = root->i_mode;
	root_node->fn_size = root->i_size;
	root_node->fn_uid = pmp->pm_uid;
	root_node->fn_gid = pmp->pm_gid;
	root_node->fn_dev = NO_DEV;

	/* We implement fdr_peek (page-assembling file peek), so advertise
	 * RES_HASPEEK for mmap()/demand-paged exec(); add RES_RDONLY when the
	 * mount is read-only. */
	*res_flags = (readonly ? RES_RDONLY : RES_NOFLAGS) | RES_HASPEEK;

	return OK;
}

/*===========================================================================*
 *				fs_unmount				     *
 *===========================================================================*/
void fs_unmount(void)
{
	struct inode *root;

	if ((root = find_inode(ROOT_INODE)) != NULL)
		put_inode(root);

	upcase_free();

	/* Clear the volume-dirty flag: all changes are about to be flushed. */
	if (!pmp->pm_rdonly)
		set_volume_dirty(0);

	lmfs_flushall();
	bdev_close(pmp->pm_dev);
	lmfs_invalidate(pmp->pm_dev);

	mounted = FALSE;
	pmp = NULL;
}

/*===========================================================================*
 *				fs_mountpt				     *
 *===========================================================================*/
int fs_mountpt(ino_t ino_nr)
{
/* Check whether the given node may serve as a mount point. */
	struct inode *rip;

	if ((rip = find_inode(ino_nr)) == NULL)
		return EINVAL;

	if (!(rip->i_attrs & EXFAT_ATTR_DIRECTORY))
		return ENOTDIR;

	if (rip->i_mountpoint)
		return EBUSY;

	rip->i_mountpoint = TRUE;

	return OK;
}
