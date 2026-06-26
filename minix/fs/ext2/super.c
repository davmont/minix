/* This file manages the super block structure.
 *
 * The entry points into this file are
 *   get_super:       search the 'superblock' table for a device
 *   read_super:      read a superblock
 *
 * Created (MFS based):
 *   February 2010 (Evgeniy Ivanov)
 */

#include "fs.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <minix/com.h>
#include <minix/u64.h>
#include <minix/bdev.h>
#include <machine/param.h>
#include <machine/vmparam.h>
#include <sys/mman.h>
#include "buf.h"
#include "inode.h"
#include "super.h"
#include "const.h"

static u32_t ext2_count_dirs(struct super_block *sp);

static void super_copy(register struct super_block *dest, register
	struct super_block *source);
static void copy_group_descriptors(struct super_block *sp, void *dest,
	void *source, unsigned int ngroups);

static off_t super_block_offset;


/*===========================================================================*
 *                              get_super                                    *
 *===========================================================================*/
struct super_block *get_super(
  dev_t dev           /* device number whose super_block is sought */
)
{
  if (dev == NO_DEV)
	panic("request for super_block of NO_DEV");
  if (superblock->s_dev != dev)
	panic("wrong superblock: 0x%x", (int) dev);

  return(superblock);
}


/*===========================================================================*
 *                              get_block_size                               *
 *===========================================================================*/
unsigned int get_block_size(dev_t dev)
{
  if (dev == NO_DEV)
	panic("request for block size of NO_DEV");
  return(lmfs_fs_block_size());
}

static struct group_desc *ondisk_group_descs;

/*===========================================================================*
 *                              read_super                                   *
 *===========================================================================*/
int read_super(sp)
register struct super_block *sp; /* pointer to a superblock */
{
  /* Read a superblock. */
  dev_t dev;
  int r;
  /* group descriptors, sp->s_group_desc points to this. */
  static struct group_desc *group_descs;
  block64_t gd_size; /* group descriptors table size in blocks */
  u64_t gdt_position;
  size_t off, chunk;

  ondisk_superblock = (struct super_block *)mmap(NULL, SUPER_SIZE_D,
	PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);
  if (ondisk_superblock == MAP_FAILED)
	panic("can't allocate buffer for super block");

  dev = sp->s_dev;              /* save device (will be overwritten by copy) */
  if (dev == NO_DEV)
	panic("request for super_block of NO_DEV");

  if (opt.block_with_super == 0) {
	super_block_offset = SUPER_BLOCK_BYTES;
  } else {
	/* The block number here uses 1k units */
	super_block_offset = opt.block_with_super * 1024;
  }

  r = bdev_read(dev, super_block_offset, (char*) ondisk_superblock,
	SUPER_SIZE_D, BDEV_NOFLAGS);

  if (r != SUPER_SIZE_D)
	return(EINVAL);

  super_copy(sp, ondisk_superblock);

  sp->s_dev = NO_DEV;           /* restore later */

  if (sp->s_magic != SUPER_MAGIC)
	return(EINVAL);

  sp->s_block_size = 1024*(1<<sp->s_log_block_size);

  if (sp->s_block_size < PAGE_SIZE) {
	printf("data block size (%u) is invalid\n", sp->s_block_size);
	return(EINVAL);
  }

  if ((sp->s_block_size % 512) != 0)
	return(EINVAL);

  if (SUPER_SIZE_D > sp->s_block_size)
	return(EINVAL);

  /* Variable added for convinience (i_blocks counts 512-byte blocks). */
  sp->s_sectors_in_block = sp->s_block_size / 512;

  /* TODO: this code is for revision 1 (but bw compatible with 0)
   * inode must be power of 2 and smaller, than block size.
   */
  if ((EXT2_INODE_SIZE(sp) & (EXT2_INODE_SIZE(sp) - 1)) != 0
      || EXT2_INODE_SIZE(sp) > sp->s_block_size) {
	printf("superblock->s_inode_size is incorrect...\n");
	return(EINVAL);
  }

  sp->s_blocksize_bits = sp->s_log_block_size + 10;
  sp->s_max_size = ext2_max_size(sp->s_block_size);
  sp->s_inodes_per_block = sp->s_block_size / EXT2_INODE_SIZE(sp);
  if (sp->s_inodes_per_block == 0 || sp->s_inodes_per_group == 0) {
	printf("either inodes_per_block or inodes_per_group count is 0\n");
	return(EINVAL);
  }

  sp->s_itb_per_group = sp->s_inodes_per_group / sp->s_inodes_per_block;

  /* Group descriptors are 32 bytes, or 64 (s_desc_size in the on-disk
   * superblock) when the 64bit feature is present.  s_gd_size is the on-disk
   * descriptor size and the stride of the in-core descriptor array. */
  if (HAS_INCOMPAT_FEATURE(sp, INCOMPAT_64BIT)) {
	u16_t ds;
	memcpy(&ds, (u8_t *) sp + 0xFE, sizeof(ds));	/* s_desc_size @ 0xFE */
	sp->s_gd_size = (ds >= 32) ? ds : 32;
  } else {
	sp->s_gd_size = 32;
  }
  sp->s_desc_per_block = sp->s_block_size / sp->s_gd_size;

  /* On a 64bit volume the total block count exceeds 32 bits; assemble the full
   * value from s_blocks_count (low) and s_blocks_count_hi (@0x150). */
  if (HAS_INCOMPAT_FEATURE(sp, INCOMPAT_64BIT)) {
	u32_t hi;
	memcpy(&hi, (u8_t *) sp + 0x150, sizeof(hi));	/* s_blocks_count_hi */
	sp->s_blocks_count_full = (block64_t) sp->s_blocks_count |
	    ((block64_t) hi << 32);
  } else {
	sp->s_blocks_count_full = sp->s_blocks_count;
  }

  sp->s_groups_count = ((sp->s_blocks_count_full - sp->s_first_data_block - 1)
				/ sp->s_blocks_per_group) + 1;

  /* ceil(groups_count/desc_per_block) */
  sp->s_gdb_count = (sp->s_groups_count + sp->s_desc_per_block - 1)
			/ sp->s_desc_per_block;

  gd_size = sp->s_gdb_count * sp->s_block_size;

  /* The in-core array holds the descriptors at s_gd_size stride; gd_size (the
   * whole on-disk GDT) is always large enough to hold them. */
  if(!(group_descs = malloc(gd_size)))
	panic("can't allocate group desc array");
  ondisk_group_descs = mmap(NULL, gd_size,
	PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);
  if (ondisk_group_descs == MAP_FAILED)
	panic("can't allocate group desc array");

  /* s_first_data_block (block number, where superblock is stored)
   * is 1 for 1Kb blocks and 0 for larger blocks.
   * For fs with 1024-byte blocks first 1024 bytes (block0) used by MBR,
   * and block1 stores superblock. When block size is larger, block0 stores
   * both MBR and superblock, but gdt lives in next block anyway.
   * If sb=N was specified, then gdt is stored in N+1 block, the block number
   * here uses 1k units.
   *
   */
  if (opt.block_with_super == 0) {
	gdt_position = (sp->s_first_data_block + 1) * sp->s_block_size;
  } else {
	gdt_position = (opt.block_with_super + 1) * 1024;
  }

  /* The driver requires contiguous memory chunks, so use page granularity. */
  for (off = 0; off < gd_size; off += chunk) {
	chunk = gd_size - off;
	if (chunk > PAGE_SIZE)
		chunk = PAGE_SIZE;

	r = bdev_read(dev, gdt_position + off,
	    (char *)ondisk_group_descs + off, chunk, BDEV_NOFLAGS);
	if (r != (ssize_t)chunk) {
		printf("Can not read group descriptors\n");
		return(EINVAL);
	}
  }

  /* TODO: check descriptors we just read */

  copy_group_descriptors(sp, group_descs, ondisk_group_descs,
	sp->s_groups_count);
  sp->s_group_desc = group_descs;

  /* Make a few basic checks to see if super block looks reasonable. */
  if (sp->s_inodes_count < 1 || sp->s_blocks_count < 1) {
	printf("not enough inodes or data blocks, \n");
	return(EINVAL);
  }

  sp->s_dirs_counter = ext2_count_dirs(sp);

  /* Start block search from this block.
   * We skip superblock (1 block), group descriptors blocks (sp->s_gdb_count)
   * block and inode bitmaps (2 blocks) and inode table.
   */
  sp->s_bsearch = sp->s_first_data_block + 1 + sp->s_gdb_count + 2
			+ sp->s_itb_per_group;

  sp->s_igsearch = 0;

  sp->s_dev = dev; /* restore device number */
  return(OK);
}


/*===========================================================================*
 *                              write_super				     *
 *===========================================================================*/
void write_super(sp)
struct super_block *sp; /* pointer to a superblock */
{
/* Write a superblock and gdt. */
  int r;
  block64_t gd_size; /* group descriptors table size in blocks */
  u64_t gdt_position;
  size_t off, chunk;

  if (sp->s_rd_only)
	panic("can't write superblock on read-only filesys.");

  if (sp->s_dev == NO_DEV)
	panic("request to write super_block, but NO_DEV");

  /* On a metadata_csum volume, refresh the group-descriptor checksums (the
   * bitmap checksums and counts they cover have just been updated) before the
   * descriptors are written, and the superblock checksum last of all. */
  if (ext2_has_csum(sp)) {
	unsigned int g;
	for (g = 0; g < sp->s_groups_count; g++)
		ext2_group_desc_csum_set(sp, g, get_group_desc(g));
  }
  ext2_super_csum_set(sp);

  super_copy(ondisk_superblock, sp);

  r = bdev_write(sp->s_dev, super_block_offset, (char *) sp, SUPER_SIZE_D,
	BDEV_NOFLAGS);
  if (r != SUPER_SIZE_D)
	printf("ext2: Warning, failed to write superblock to the disk!\n");

  if (group_descriptors_dirty) {
	/* Locate the appropriate super_block. */
	gd_size = sp->s_gdb_count * sp->s_block_size;

	if (opt.block_with_super == 0) {
		gdt_position = (sp->s_first_data_block + 1) * sp->s_block_size;
	} else {
		gdt_position = (opt.block_with_super + 1) * 1024;
	}

        copy_group_descriptors(sp, ondisk_group_descs, sp->s_group_desc,
			       sp->s_groups_count);

	/* As above. Yes, lame. */
	for (off = 0; off < gd_size; off += chunk) {
		chunk = gd_size - off;
		if (chunk > PAGE_SIZE)
			chunk = PAGE_SIZE;

		r = bdev_write(sp->s_dev, gdt_position + off,
		    (char *)ondisk_group_descs + off, chunk, BDEV_NOFLAGS);
		if (r != (ssize_t)chunk) {
			printf("Can not write group descriptors\n");
		}
	}

	group_descriptors_dirty = 0;
  }
}


/*===========================================================================*
 *                              get_group_desc                               *
 *===========================================================================*/
struct group_desc* get_group_desc(unsigned int bnum)
{
  if (bnum >= superblock->s_groups_count) {
	printf("ext2, get_group_desc: wrong bnum (%d) requested\n", bnum);
	return NULL;
  }
  /* The in-core array is strided by s_gd_size (32 or 64), not sizeof. */
  return (struct group_desc *)((char *)superblock->s_group_desc +
	(size_t) bnum * superblock->s_gd_size);
}


/*===========================================================================*
 *		group-descriptor 48-bit block pointer accessors	     *
 *===========================================================================*/
/* On a 64bit volume the bitmap/inode-table block numbers are split into a low
 * 32-bit field and a high field; assemble the full value (the high half exists
 * only when s_gd_size is 64). */
block64_t ext2_gd_block_bitmap(struct super_block *sp, struct group_desc *gd)
{
  block64_t v = gd->block_bitmap;
  if (sp->s_gd_size >= 64)
	v |= (block64_t) gd->block_bitmap_hi << 32;
  return v;
}

block64_t ext2_gd_inode_bitmap(struct super_block *sp, struct group_desc *gd)
{
  block64_t v = gd->inode_bitmap;
  if (sp->s_gd_size >= 64)
	v |= (block64_t) gd->inode_bitmap_hi << 32;
  return v;
}

block64_t ext2_gd_inode_table(struct super_block *sp, struct group_desc *gd)
{
  block64_t v = gd->inode_table;
  if (sp->s_gd_size >= 64)
	v |= (block64_t) gd->inode_table_hi << 32;
  return v;
}

/* Full 64-bit free-block count: the on-disk s_free_blocks_count holds the low
 * 32 bits; the high half (s_free_blocks_count_hi @0x158) is preserved across
 * writes, so combine them.  (We maintain only the low half, which is correct as
 * long as the count does not cross a 2^32 boundary in a single session.) */
block64_t ext2_free_blocks_count(struct super_block *sp)
{
  block64_t v = sp->s_free_blocks_count;
  if (HAS_INCOMPAT_FEATURE(sp, INCOMPAT_64BIT)) {
	u32_t hi;
	memcpy(&hi, (u8_t *) sp + 0x158, sizeof(hi));
	v |= (block64_t) hi << 32;
  }
  return v;
}


static u32_t ext2_count_dirs(struct super_block *sp)
{
  u32_t count = 0;
  unsigned int i;

  for (i = 0; i < sp->s_groups_count; i++) {
	struct group_desc *desc = get_group_desc(i);
	if (!desc)
		continue; /* TODO: fail? */
	count += desc->used_dirs_count;
  }
  return count;
}


/*===========================================================================*
 *                              ext2_max_size                                *
 *===========================================================================*/
/* There are several things, which affect max filesize:
 * - inode.i_blocks (512-byte blocks) is limited to (2^32 - 1).
 * - number of addressed direct, single, double and triple indirect blocks.
 * Number of addressed blocks depends on block_size only, thus unlike in
 * linux (ext2_max_size) we do not make calculations, but use constants
 * for different block sizes. Calculations (gcc code) are commented.
 * Note: linux ext2_max_size makes calculated based on shifting, not
 * arithmetics.
 * (!!!)Note: constants hardly tight to EXT2_NDIR_BLOCKS, but I doubt its value
 * will be changed someday. So if it's changed, then just recalculate constatns.
 * Anyway this function is safe for any change.
 * Note: there is also limitation from VFS (to LONG_MAX, i.e. 2GB).
 */
off_t ext2_max_size(int block_size)
{
  /* 12 is EXT2_NDIR_BLOCKS used in calculations. */
  if (EXT2_NDIR_BLOCKS != 12)
	panic("ext2_max_size needs modification!");
  switch(block_size) {
	case 1024: return LONG_MAX; /* actually 17247252480 */
	case 2048: return LONG_MAX; /* 275415851008 */
	case 4096: return LONG_MAX; /* 2194719883264 */
	default: {
		ext2_debug("ext2_max_size: Unsupported block_size! \
				Assuming bs is 1024 bytes\n");
		return 67383296L;
	}
  }
#if 0
  long addr_in_block = block_size/4; /* 4 bytes per addr */
  long sectors_in_block = block_size/512;
  long long meta_blocks; /* single, double and triple indirect blocks */
  unsigned long long out_range_s; /* max blocks addressed by inode */
  unsigned long long max_bytes;
  unsigned long long upper_limit;

  /* 1 indirect block, 1 + addr_in_block dindirect and 1 + addr_in_block +
   * + addr_in_block*addr_in_block triple indirect blocks */
  meta_blocks = 2*addr_in_block + addr_in_block*addr_in_block + 3;
  out_range_s = EXT2_NDIR_BLOCKS + addr_in_block + addr_in_block * addr_in_block
                + addr_in_block * addr_in_block * addr_in_block;
  max_bytes = out_range_s * block_size;

  upper_limit = (1LL << 32) - 1; /* max 512-byte blocks by i_blocks */
  upper_limit /= sectors_in_block; /* total block_size blocks */
  upper_limit -= meta_blocks; /* total data blocks */
  upper_limit *= (long long)block_size; /* max size in bytes */

  if (max_bytes > upper_limit)
	max_bytes = upper_limit;

  /* Limit s_max_size to LONG_MAX */
  if (max_bytes > LONG_MAX)
	max_bytes = LONG_MAX;

  return max_bytes;
#endif
}


/*===========================================================================*
 *				super_copy				     *
 *===========================================================================*/
static void super_copy(
  register struct super_block *dest,
  register struct super_block *source
)
/* Note: we don't convert stuff, used in ext3. */
{
/* Copy super_block to the in-core table, swapping bytes if need be. */
  if (le_CPU) {
	/* Just use memcpy */
	memcpy(dest, source, SUPER_SIZE_D);
	return;
  }
  dest->s_inodes_count = conv4(le_CPU, source->s_inodes_count);
  dest->s_blocks_count = conv4(le_CPU, source->s_blocks_count);
  dest->s_r_blocks_count = conv4(le_CPU, source->s_r_blocks_count);
  dest->s_free_blocks_count = conv4(le_CPU, source->s_free_blocks_count);
  dest->s_free_inodes_count = conv4(le_CPU, source->s_free_inodes_count);
  dest->s_first_data_block = conv4(le_CPU, source->s_first_data_block);
  dest->s_log_block_size = conv4(le_CPU, source->s_log_block_size);
  dest->s_log_frag_size = conv4(le_CPU, source->s_log_frag_size);
  dest->s_blocks_per_group = conv4(le_CPU, source->s_blocks_per_group);
  dest->s_frags_per_group = conv4(le_CPU, source->s_frags_per_group);
  dest->s_inodes_per_group = conv4(le_CPU, source->s_inodes_per_group);
  dest->s_mtime = conv4(le_CPU, source->s_mtime);
  dest->s_wtime = conv4(le_CPU, source->s_wtime);
  dest->s_mnt_count = conv2(le_CPU, source->s_mnt_count);
  dest->s_max_mnt_count = conv2(le_CPU, source->s_max_mnt_count);
  dest->s_magic = conv2(le_CPU, source->s_magic);
  dest->s_state = conv2(le_CPU, source->s_state);
  dest->s_errors = conv2(le_CPU, source->s_errors);
  dest->s_minor_rev_level = conv2(le_CPU, source->s_minor_rev_level);
  dest->s_lastcheck = conv4(le_CPU, source->s_lastcheck);
  dest->s_checkinterval = conv4(le_CPU, source->s_checkinterval);
  dest->s_creator_os = conv4(le_CPU, source->s_creator_os);
  dest->s_rev_level = conv4(le_CPU, source->s_rev_level);
  dest->s_def_resuid = conv2(le_CPU, source->s_def_resuid);
  dest->s_def_resgid = conv2(le_CPU, source->s_def_resgid);
  dest->s_first_ino = conv4(le_CPU, source->s_first_ino);
  dest->s_inode_size = conv2(le_CPU, source->s_inode_size);
  dest->s_block_group_nr = conv2(le_CPU, source->s_block_group_nr);
  dest->s_feature_compat = conv4(le_CPU, source->s_feature_compat);
  dest->s_feature_incompat = conv4(le_CPU, source->s_feature_incompat);
  dest->s_feature_ro_compat = conv4(le_CPU, source->s_feature_ro_compat);
  memcpy(dest->s_uuid, source->s_uuid, sizeof(dest->s_uuid));
  memcpy(dest->s_volume_name, source->s_volume_name,
	 sizeof(dest->s_volume_name));
  memcpy(dest->s_last_mounted, source->s_last_mounted,
	 sizeof(dest->s_last_mounted));
  dest->s_algorithm_usage_bitmap =
			conv4(le_CPU, source->s_algorithm_usage_bitmap);
  dest->s_prealloc_blocks = source->s_prealloc_blocks;
  dest->s_prealloc_dir_blocks = source->s_prealloc_dir_blocks;
  dest->s_padding1 = conv2(le_CPU, source->s_padding1);
}


/*===========================================================================*
 *				gd_copy 				     *
 *===========================================================================*/
static void gd_copy(
  struct super_block *sp,
  void *dest,
  void *source
)
{
/* Copy one group descriptor (s_gd_size bytes), swapping bytes if need be. */
  memcpy(dest, source, sp->s_gd_size);
  if (!le_CPU) {
	/* Big-endian host: byte-swap the fields in place. */
	struct group_desc *d = dest;
	d->block_bitmap = conv4(le_CPU, d->block_bitmap);
	d->inode_bitmap = conv4(le_CPU, d->inode_bitmap);
	d->inode_table = conv4(le_CPU, d->inode_table);
	d->free_blocks_count = conv2(le_CPU, d->free_blocks_count);
	d->free_inodes_count = conv2(le_CPU, d->free_inodes_count);
	d->used_dirs_count = conv2(le_CPU, d->used_dirs_count);
	if (sp->s_gd_size >= 64) {
		d->block_bitmap_hi = conv4(le_CPU, d->block_bitmap_hi);
		d->inode_bitmap_hi = conv4(le_CPU, d->inode_bitmap_hi);
		d->inode_table_hi = conv4(le_CPU, d->inode_table_hi);
	}
  }
}


/*===========================================================================*
 *			copy_group_descriptors  			     *
 *===========================================================================*/
static void copy_group_descriptors(
  struct super_block *sp,
  void *dest,
  void *source,
  unsigned int ngroups
)
{
  unsigned int i;
  for (i = 0; i < ngroups; i++)
	gd_copy(sp, (char *)dest + (size_t)i * sp->s_gd_size,
	    (char *)source + (size_t)i * sp->s_gd_size);
}
