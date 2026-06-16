/* Mount / unmount for the vfat server.
 *
 * The geometry computation is ported from NetBSD's msdosfs_vfsops.c
 * (mountmsdosfs).  vfat is read-only, so we open the device read-only and
 * report RES_RDONLY back to VFS.
 */
#include "fs.h"
#include <minix/vfsif.h>
#include <sys/stat.h>

#define DEV_BSIZE	512	/* assumed/required sector size for phase 1 */

/*===========================================================================*
 *				v_ffs					     *
 *===========================================================================*/
static int v_ffs(unsigned long v)
{
/* Return the 1-based index of the least significant set bit (like ffs(3)),
 * or 0 if v == 0.  Avoids depending on libc's ffs() in this service. */
	int i;

	if (v == 0)
		return 0;
	for (i = 1; !(v & 1); i++)
		v >>= 1;
	return i;
}

/*===========================================================================*
 *				parse_bpb				     *
 *===========================================================================*/
static int parse_bpb(const char *secbuf)
{
/* Parse the boot sector / BPB at secbuf into the global fat_mount (pmp).
 * Returns OK or an errno.  Mirrors NetBSD mountmsdosfs().
 */
	const union bootsector *bsp = (const union bootsector *) secbuf;
	const struct byte_bpb33 *b33;
	const struct byte_bpb50 *b50;
	const struct byte_bpb710 *b710;
	unsigned int SecPerClust;

	b33 = (const struct byte_bpb33 *) bsp->bs33.bsBPB;
	b50 = (const struct byte_bpb50 *) bsp->bs50.bsBPB;
	b710 = (const struct byte_bpb710 *) bsp->bs710.bsBPB;

	if ((uint8_t) bsp->bs50.bsBootSectSig0 != BOOTSIG0 ||
	    (uint8_t) bsp->bs50.bsBootSectSig1 != BOOTSIG1)
		return EINVAL;

	memset(pmp, 0, sizeof(*pmp));

	SecPerClust = b50->bpbSecPerClust;
	pmp->pm_BytesPerSec = getushort(b50->bpbBytesPerSec);
	pmp->pm_ResSectors = getushort(b50->bpbResSectors);
	pmp->pm_FATs = b50->bpbFATs;
	pmp->pm_RootDirEnts = getushort(b50->bpbRootDirEnts);
	pmp->pm_Sectors = getushort(b50->bpbSectors);
	pmp->pm_FATsecs = getushort(b50->bpbFATsecs);
	pmp->pm_SecPerTrack = getushort(b50->bpbSecPerTrack);
	pmp->pm_Heads = getushort(b50->bpbHeads);
	pmp->pm_Media = b50->bpbMedia;

	if (!pmp->pm_BytesPerSec || !SecPerClust || pmp->pm_SecPerTrack > 63)
		return EINVAL;

	/* Phase 1: only 512-byte sectors are supported. */
	if (pmp->pm_BytesPerSec != DEV_BSIZE)
		return EINVAL;

	if (pmp->pm_Sectors == 0) {
		pmp->pm_HiddenSects = getulong(b50->bpbHiddenSecs);
		pmp->pm_HugeSectors = getulong(b50->bpbHugeSectors);
	} else {
		pmp->pm_HiddenSects = getushort(b33->bpbHiddenSecs);
		pmp->pm_HugeSectors = pmp->pm_Sectors;
	}

	if (pmp->pm_RootDirEnts == 0) {
		/* FAT32. */
		unsigned short vers = getushort(b710->bpbFSVers);

		if (pmp->pm_Sectors || pmp->pm_FATsecs || vers)
			return EINVAL;
		pmp->pm_fatmask = FAT32_MASK;
		pmp->pm_fatmult = 4;
		pmp->pm_fatdiv = 1;
		pmp->pm_FATsecs = getulong(b710->bpbBigFATsecs);
		if ((getushort(b710->bpbExtFlags) & FATMIRROR) == 0)
			; /* mirrored */
		else
			pmp->pm_curfat = getushort(b710->bpbExtFlags) & FATNUM;
	}

	if (pmp->pm_FATsecs == 0)
		return EINVAL;

	pmp->pm_fatblk = pmp->pm_ResSectors;
	if (FAT32(pmp)) {
		pmp->pm_rootdirblk = getulong(b710->bpbRootClust);
		pmp->pm_firstcluster = pmp->pm_fatblk +
		    (pmp->pm_FATs * pmp->pm_FATsecs);
		pmp->pm_fsinfo = getushort(b710->bpbFSInfo);
	} else {
		pmp->pm_rootdirblk = pmp->pm_fatblk +
		    (pmp->pm_FATs * pmp->pm_FATsecs);
		pmp->pm_rootdirsize = (pmp->pm_RootDirEnts *
		    sizeof(struct direntry) + pmp->pm_BytesPerSec - 1) /
		    pmp->pm_BytesPerSec;	/* in sectors */
		pmp->pm_firstcluster = pmp->pm_rootdirblk + pmp->pm_rootdirsize;
	}

	pmp->pm_nmbrofclusters =
	    (pmp->pm_HugeSectors - pmp->pm_firstcluster) / SecPerClust;
	pmp->pm_maxcluster = pmp->pm_nmbrofclusters + 1;
	pmp->pm_fatsize = pmp->pm_FATsecs * pmp->pm_BytesPerSec;

	if (pmp->pm_fatmask == 0) {
		if (pmp->pm_maxcluster <=
		    ((CLUST_RSRVD - CLUST_FIRST) & FAT12_MASK)) {
			pmp->pm_fatmask = FAT12_MASK;
			pmp->pm_fatmult = 3;
			pmp->pm_fatdiv = 2;
		} else {
			pmp->pm_fatmask = FAT16_MASK;
			pmp->pm_fatmult = 2;
			pmp->pm_fatdiv = 1;
		}
	}

	if (FAT12(pmp))
		pmp->pm_fatblocksize = 3 * pmp->pm_BytesPerSec;
	else
		pmp->pm_fatblocksize = pmp->pm_BytesPerSec;

	pmp->pm_fatblocksec = pmp->pm_fatblocksize / pmp->pm_BytesPerSec;
	pmp->pm_bnshift = v_ffs(pmp->pm_BytesPerSec) - 1;

	pmp->pm_bpcluster = SecPerClust * pmp->pm_BytesPerSec;
	pmp->pm_crbomask = pmp->pm_bpcluster - 1;
	pmp->pm_cnshift = v_ffs(pmp->pm_bpcluster) - 1;

	if (pmp->pm_bpcluster ^ (1UL << pmp->pm_cnshift))
		return EINVAL;		/* cluster size not a power of two */

	return OK;
}

/*===========================================================================*
 *				fs_mount				     *
 *===========================================================================*/
int fs_mount(dev_t dev, unsigned int flags, struct fsdriver_node *root_node,
	unsigned int *res_flags)
{
	struct buf *bp;
	struct inode *root;
	int r, readonly;

	readonly = (flags & REQ_RDONLY) ? 1 : 0;

	if (bdev_open(dev, readonly ? BDEV_R_BIT : (BDEV_R_BIT | BDEV_W_BIT))
	    != OK)
		return EINVAL;

	pmp = &fat_mount;

	/*
	 * Read the boot sector through the block cache at the provisional
	 * sector size (512).  If the on-disk BytesPerSec differs we bail out
	 * (phase 1 limitation), so the provisional size is always correct.
	 */
	lmfs_set_blocksize(DEV_BSIZE);

	if ((bp = get_block(dev, 0, NORMAL)) == NULL) {
		bdev_close(dev);
		return EINVAL;
	}

	r = parse_bpb(b_data(bp));
	put_block(bp);

	if (r != OK) {
		bdev_close(dev);
		return r;
	}

	pmp->pm_dev = dev;
	pmp->pm_rdonly = readonly;
	pmp->pm_uid = 0;
	pmp->pm_gid = 0;
	pmp->pm_mask = DEFAULT_FMASK;
	pmp->pm_dirmask = DEFAULT_DMASK;
	pmp->pm_inusemap = NULL;

	/* Build the in-use cluster bitmap (needed for allocation + free count). */
	if ((r = fill_inusemap()) != OK) {
		bdev_close(dev);
		return r;
	}

	/* Advertise total/used cluster counts to libminixfs. */
	lmfs_set_blockusage(pmp->pm_nmbrofclusters,
	    pmp->pm_nmbrofclusters - pmp->pm_freeclustercount);

	mounted = TRUE;

	/* Build the root inode and report it to VFS. */
	if ((root = get_root_inode()) == NULL) {
		bdev_close(dev);
		mounted = FALSE;
		return EINVAL;
	}

	root_node->fn_ino_nr = root->i_num;
	root_node->fn_mode = root->i_mode;
	root_node->fn_size = root->i_size;
	root_node->fn_uid = pmp->pm_uid;
	root_node->fn_gid = pmp->pm_gid;
	root_node->fn_dev = NO_DEV;

	/* If mounted read-only, tell VFS so it rejects writes up front. */
	*res_flags = readonly ? RES_RDONLY : RES_NOFLAGS;

	return OK;
}

/*===========================================================================*
 *				fs_unmount				     *
 *===========================================================================*/
void fs_unmount(void)
{
	struct inode *root;

	/* Release the root inode reference taken at mount time. */
	if ((root = find_inode(ROOT_INODE)) != NULL)
		put_inode(root);

	update_fsinfo();
	lmfs_flushall();

	bdev_close(pmp->pm_dev);
	lmfs_invalidate(pmp->pm_dev);

	if (pmp->pm_inusemap != NULL) {
		free(pmp->pm_inusemap);
		pmp->pm_inusemap = NULL;
	}

	mounted = FALSE;
	pmp = NULL;
}

/*===========================================================================*
 *				fs_mountpt				     *
 *===========================================================================*/
int fs_mountpt(ino_t ino_nr)
{
/* Check whether the given node may serve as a mount point: it must be a
 * directory that is not already a mount point.
 */
	struct inode *rip;

	if ((rip = find_inode(ino_nr)) == NULL)
		return EINVAL;

	if (!(rip->i_attrs & ATTR_DIRECTORY))
		return ENOTDIR;

	if (rip->i_mountpoint)
		return EBUSY;

	rip->i_mountpoint = TRUE;

	return OK;
}
