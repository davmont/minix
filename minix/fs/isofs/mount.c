#include "inc.h"
#include <minix/vfsif.h>

int fs_mount(dev_t dev, unsigned int __unused flags,
	struct fsdriver_node *root_node, unsigned int *res_flags)
{
	int r;

	fs_dev = dev;

	/* Open the device the file system lives on in read only mode */
	if (bdev_open(fs_dev, BDEV_R_BIT) != OK)
		return EINVAL;

	/* Read the superblock */
	r = read_vds(&v_pri, fs_dev);
	if (r != OK) {
		bdev_close(fs_dev);
		return r;
	}

	/* Return some root inode properties */
	root_node->fn_ino_nr = v_pri.inode_root->i_stat.st_ino;
	root_node->fn_mode = v_pri.inode_root->i_stat.st_mode;
	root_node->fn_size = v_pri.inode_root->i_stat.st_size;
	root_node->fn_uid = SYS_UID; /* Always root */
	root_node->fn_gid = SYS_GID; /* wheel */
	root_node->fn_dev = NO_DEV;

	/*
	 * Deliberately NOT RES_HASPEEK.  RES_HASPEEK would let VFS use this fs
	 * for file mmap() and demand-paged exec(), but isofs cannot serve those
	 * correctly: its block peek is plain lmfs_bio (raw device blocks), with
	 * no file-offset -> CD-extent translation (cf. read_extent_block() in
	 * fs_read()).  Demand-paging would therefore fetch the wrong device
	 * blocks and the faulted-in pages would be garbage, crashing every
	 * binary exec'd from an isofs root.  Supporting mmap here needs a real
	 * extent-translating bpeek first.
	 */
	*res_flags = RES_NOFLAGS;

	return r;
}

int fs_mountpt(ino_t ino_nr)
{
	/*
	 * This function looks up the mount point, it checks the condition
	 * whether the partition can be mounted on the inode or not.
	 */
	struct inode *rip;

	if ((rip = get_inode(ino_nr)) == NULL)
		return EINVAL;

	if (rip->i_mountpoint)
		return EBUSY;

	/* The inode must be a directory. */
	if ((rip->i_stat.st_mode & I_TYPE) != I_DIRECTORY)
		return ENOTDIR;

	rip->i_mountpoint = TRUE;

	return OK;
}

void fs_unmount(void)
{
	release_vol_pri_desc(&v_pri);	/* Release the super block */

	bdev_close(fs_dev);

	if (check_inodes() == FALSE)
		puts("ISOFS: unmounting with in-use inodes!\n");
}
