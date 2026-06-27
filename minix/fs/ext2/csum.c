/* ext4 metadata_csum support: crc32c checksums over on-disk metadata.
 *
 * When a volume has the RO_COMPAT_METADATA_CSUM feature, every metadata
 * structure carries a crc32c checksum that must be kept up to date on write or
 * e2fsck (and the Linux kernel) will reject it.  This file computes and stores
 * those checksums for each structure; the call sites live in super.c, balloc.c,
 * ialloc.c, inode.c, extent.c and path.c.
 *
 * All algorithms here were verified byte-for-byte against e2fsprogs 1.47.3:
 *   - seed       = s_checksum_seed if INCOMPAT_CSUM_SEED, else crc32c(~0, uuid)
 *   - superblock = crc32c(~0, sb[0 .. s_checksum))                   -> s_checksum
 *   - group desc = crc32c(seed, le32(group)); crc32c(., desc) with bg_checksum
 *                  zeroed                                       -> bg_checksum&0xffff
 *   - block/ino  = crc32c(seed, bitmap[0 .. bits/8])           -> bg_*_csum_lo&0xffff
 *     bitmap
 *   - inode      = crc32c(seed, le32(ino)); crc32c(., le32(gen)); crc32c(., inode)
 *                  with i_checksum_lo/hi zeroed   -> i_checksum_lo (+hi if room)
 *   - dir block  = crc32c(seed, le32(ino)); crc32c(., le32(gen));
 *                  crc32c(., block[0 .. blocksize-12])   -> tail det_checksum
 *   - extent blk = crc32c(seed, le32(ino)); crc32c(., le32(gen));
 *                  crc32c(., block[0 .. blocksize-4])    -> tail eb_checksum
 *
 * Created:
 *   June 2026 (ext4 metadata_csum support)
 */

#include "fs.h"
#include <string.h>
#include "buf.h"
#include "inode.h"
#include "super.h"
#include "const.h"

/* Byte offsets of fields the in-core struct super_block does not name but that
 * are present on disk (the struct mirrors the disk for its first 1024 bytes). */
#define SB_UUID_OFF		0x68	/* s_uuid[16] */
#define SB_CSUM_SEED_OFF	0x270	/* s_checksum_seed */
#define SB_CSUM_OFF		0x3FC	/* s_checksum (last word of the SB) */

/* Field offsets within a 32-byte group descriptor. */
#define GD_FLAGS_OFF		18	/* bg_flags */
#define GD_BBITMAP_CSUM_OFF	24	/* bg_block_bitmap_csum_lo */
#define GD_IBITMAP_CSUM_OFF	26	/* bg_inode_bitmap_csum_lo */
#define GD_ITABLE_UNUSED_OFF	28	/* bg_itable_unused_lo */
#define GD_CHECKSUM_OFF		30	/* bg_checksum */
/* High halves of the bitmap checksums, present only in 64-byte descriptors. */
#define GD_BBITMAP_CSUM_HI_OFF	56	/* bg_block_bitmap_csum_hi */
#define GD_IBITMAP_CSUM_HI_OFF	58	/* bg_inode_bitmap_csum_hi */

/* bg_flags bits (uninit_bg / metadata_csum lazy-init bookkeeping). */
#define EXT2_BG_INODE_UNINIT	0x0001	/* inode table/bitmap not initialised */
#define EXT2_BG_BLOCK_UNINIT	0x0002	/* block bitmap not initialised */

/* Field offsets within an on-disk inode. */
#define INO_GENERATION_OFF	0x64
#define INO_CHECKSUM_LO_OFF	0x7C	/* osd2.linux2.l_i_checksum_lo */
#define INO_EXTRA_ISIZE_OFF	0x80	/* i_extra_isize (only if inode > 128) */
#define INO_CHECKSUM_HI_OFF	0x82	/* i_checksum_hi (in the extra area) */
/* i_checksum_hi exists when the inode is larger than 128 bytes and its
 * i_extra_isize records at least up to and including that field. */
#define INO_CSUM_HI_EXTRA_END	(INO_CHECKSUM_HI_OFF + 2 - EXT2_GOOD_OLD_INODE_SIZE)

/* Size of the on-disk checksum "tails" appended to dir and extent blocks. */
#define DIR_TAIL_SIZE		12	/* struct ext4_dir_entry_tail */
#define EXTENT_TAIL_SIZE	4	/* struct ext4_extent_tail (eb_checksum) */
#define EXT4_FT_DIR_CSUM	0xDE	/* det_reserved_ft marking a tail */

/*===========================================================================*
 *				ext2_has_csum				     *
 *===========================================================================*/
int ext2_has_csum(struct super_block *sp)
{
	return HAS_RO_COMPAT_FEATURE(sp, RO_COMPAT_METADATA_CSUM) ? 1 : 0;
}

/*===========================================================================*
 *				ext2_csum_seed				     *
 *===========================================================================*/
static u32_t ext2_csum_seed(struct super_block *sp)
{
/* The seed all metadata checksums (except the superblock's own) start from. */
	const u8_t *raw = (const u8_t *) sp;

	if (HAS_INCOMPAT_FEATURE(sp, INCOMPAT_CSUM_SEED)) {
		u32_t seed;
		memcpy(&seed, raw + SB_CSUM_SEED_OFF, sizeof(seed));
		return seed;
	}
	return ext2_crc32c(0xFFFFFFFF, raw + SB_UUID_OFF, 16);
}

/*===========================================================================*
 *				ext2_super_csum_set			     *
 *===========================================================================*/
void ext2_super_csum_set(struct super_block *sp)
{
/* Recompute s_checksum over the superblock (everything before the field). */
	u8_t *raw = (u8_t *) sp;
	u32_t c;

	if (!ext2_has_csum(sp))
		return;
	c = ext2_crc32c(0xFFFFFFFF, raw, SB_CSUM_OFF);
	memcpy(raw + SB_CSUM_OFF, &c, sizeof(c));
}

/*===========================================================================*
 *				ext2_group_desc_csum_set		     *
 *===========================================================================*/
void ext2_group_desc_csum_set(struct super_block *sp, u32_t group,
	struct group_desc *gd)
{
/* Recompute bg_checksum over the descriptor (with the field itself zeroed). */
	u8_t *raw = (u8_t *) gd;
	u32_t le_group = group;		/* little-endian on amd64 */
	u16_t zero = 0, csum;
	u32_t c;

	if (!ext2_has_csum(sp))
		return;
	memcpy(raw + GD_CHECKSUM_OFF, &zero, sizeof(zero));
	c = ext2_crc32c(ext2_csum_seed(sp), &le_group, sizeof(le_group));
	c = ext2_crc32c(c, raw, sp->s_gd_size);
	csum = (u16_t)(c & 0xFFFF);
	memcpy(raw + GD_CHECKSUM_OFF, &csum, sizeof(csum));
}

/*===========================================================================*
 *				ext2_bitmap_csum_set			     *
 *===========================================================================*/
static void bitmap_csum_set(struct super_block *sp, struct group_desc *gd,
	const void *bitmap, unsigned int nbits, int lo_off, int hi_off)
{
	u32_t full;
	u16_t lo, hi;

	if (!ext2_has_csum(sp))
		return;
	full = ext2_crc32c(ext2_csum_seed(sp), bitmap, nbits / 8);
	lo = (u16_t)(full & 0xFFFF);
	memcpy((u8_t *) gd + lo_off, &lo, sizeof(lo));
	/* The high 16 bits of the checksum live in the 64-byte descriptor. */
	if (sp->s_gd_size >= 64) {
		hi = (u16_t)((full >> 16) & 0xFFFF);
		memcpy((u8_t *) gd + hi_off, &hi, sizeof(hi));
	}
}

void ext2_block_bitmap_csum_set(struct super_block *sp, struct group_desc *gd,
	const void *bitmap)
{
	bitmap_csum_set(sp, gd, bitmap, sp->s_blocks_per_group,
	    GD_BBITMAP_CSUM_OFF, GD_BBITMAP_CSUM_HI_OFF);
}

void ext2_inode_bitmap_csum_set(struct super_block *sp, struct group_desc *gd,
	const void *bitmap)
{
	bitmap_csum_set(sp, gd, bitmap, sp->s_inodes_per_group,
	    GD_IBITMAP_CSUM_OFF, GD_IBITMAP_CSUM_HI_OFF);
}

/*===========================================================================*
 *				ext2_group_inode_alloc			     *
 *===========================================================================*/
void ext2_group_inode_alloc(struct super_block *sp, struct group_desc *gd,
	unsigned int rel_ino)
{
/* Update a group descriptor's lazy-init bookkeeping when inode 'rel_ino' (the
 * 0-based index within the group) is allocated.  metadata_csum volumes track
 * bg_itable_unused -- the number of inodes at the tail of the table that have
 * never been used -- and the INODE_UNINIT flag; e2fsck and the kernel treat
 * inodes in that tail region as free, so the count must shrink to keep the
 * newly used inode out of it. */
	u8_t *raw = (u8_t *) gd;
	u16_t flags, unused, want;

	if (!ext2_has_csum(sp))
		return;

	memcpy(&flags, raw + GD_FLAGS_OFF, sizeof(flags));
	flags &= ~EXT2_BG_INODE_UNINIT;
	memcpy(raw + GD_FLAGS_OFF, &flags, sizeof(flags));

	memcpy(&unused, raw + GD_ITABLE_UNUSED_OFF, sizeof(unused));
	want = (u16_t)(sp->s_inodes_per_group - (rel_ino + 1));
	if (want < unused)
		memcpy(raw + GD_ITABLE_UNUSED_OFF, &want, sizeof(want));
}

/*===========================================================================*
 *				ext2_inode_bitmap_init			     *
 *===========================================================================*/
void ext2_inode_bitmap_init(struct super_block *sp, struct group_desc *gd,
	void *bitmap)
{
/* A lazily-initialised group (INODE_UNINIT) has an all-zero on-disk inode
 * bitmap.  When the first inode is allocated there the bitmap becomes live, so
 * the padding bits beyond s_inodes_per_group must be set to 1 (as mke2fs and
 * the kernel do) or e2fsck reports "padding at end of inode bitmap is not set".
 * Call this before setting the new inode's bit and before clearing the flag. */
	u8_t *bm = (u8_t *) bitmap;
	u16_t flags;
	unsigned int ipg, ceil;

	if (!ext2_has_csum(sp))
		return;
	memcpy(&flags, (u8_t *) gd + GD_FLAGS_OFF, sizeof(flags));
	if (!(flags & EXT2_BG_INODE_UNINIT))
		return;				/* already initialised */

	ipg = sp->s_inodes_per_group;
	ceil = (ipg + 7) / 8;			/* bytes holding valid inode bits */
	if (ipg % 8)				/* partial byte: pad its high bits */
		bm[ipg / 8] |= (u8_t)(0xFF << (ipg % 8));
	if (ceil < sp->s_block_size)
		memset(bm + ceil, 0xFF, sp->s_block_size - ceil);
}

/*===========================================================================*
 *				ext2_group_block_alloc			     *
 *===========================================================================*/
void ext2_group_block_alloc(struct super_block *sp, struct group_desc *gd)
{
/* Clear BLOCK_UNINIT once a block is allocated in the group (its bitmap is no
 * longer the lazily-initialised all-free state). */
	u8_t *raw = (u8_t *) gd;
	u16_t flags;

	if (!ext2_has_csum(sp))
		return;
	memcpy(&flags, raw + GD_FLAGS_OFF, sizeof(flags));
	flags &= ~EXT2_BG_BLOCK_UNINIT;
	memcpy(raw + GD_FLAGS_OFF, &flags, sizeof(flags));
}

/*===========================================================================*
 *				ext2_inode_csum_set			     *
 *===========================================================================*/
void ext2_inode_csum_set(struct super_block *sp, ino_t ino, void *dinode)
{
/* Set i_checksum_lo (and i_checksum_hi when the inode is large enough) over the
 * on-disk inode, with the checksum fields treated as zero. */
	u8_t *raw = (u8_t *) dinode;
	int isize = EXT2_INODE_SIZE(sp);
	int has_hi;
	u32_t inum_le = (u32_t) ino, gen_le, c;
	u16_t zero = 0, extra, lo, hi;

	if (!ext2_has_csum(sp))
		return;

	memcpy(&gen_le, raw + INO_GENERATION_OFF, sizeof(gen_le));

	has_hi = 0;
	if (isize > EXT2_GOOD_OLD_INODE_SIZE) {
		memcpy(&extra, raw + INO_EXTRA_ISIZE_OFF, sizeof(extra));
		if (extra >= INO_CSUM_HI_EXTRA_END)
			has_hi = 1;
	}

	/* Zero the checksum fields, compute, then store. */
	memcpy(raw + INO_CHECKSUM_LO_OFF, &zero, sizeof(zero));
	if (has_hi)
		memcpy(raw + INO_CHECKSUM_HI_OFF, &zero, sizeof(zero));

	c = ext2_crc32c(ext2_csum_seed(sp), &inum_le, sizeof(inum_le));
	c = ext2_crc32c(c, &gen_le, sizeof(gen_le));
	c = ext2_crc32c(c, raw, isize);

	lo = (u16_t)(c & 0xFFFF);
	memcpy(raw + INO_CHECKSUM_LO_OFF, &lo, sizeof(lo));
	if (has_hi) {
		hi = (u16_t)((c >> 16) & 0xFFFF);
		memcpy(raw + INO_CHECKSUM_HI_OFF, &hi, sizeof(hi));
	}
}

/*===========================================================================*
 *				ext2_dir_block_csum_set			     *
 *===========================================================================*/
void ext2_dir_block_csum_set(struct inode *dirp, void *block)
{
/* Install the fake checksum dir-entry tail at the end of a directory block and
 * compute its det_checksum over everything before the tail. */
	struct super_block *sp = dirp->i_sp;
	u8_t *raw = (u8_t *) block;
	u8_t *tail = raw + sp->s_block_size - DIR_TAIL_SIZE;
	u32_t inum_le = (u32_t) dirp->i_num, gen_le = dirp->i_generation, c;
	u32_t z32 = 0;
	u16_t reclen = DIR_TAIL_SIZE;

	if (!ext2_has_csum(sp))
		return;

	/* struct ext4_dir_entry_tail: zero inode, rec_len=12, ft=0xDE. */
	memcpy(tail + 0, &z32, sizeof(z32));	/* det_inode (reuse as 0) */
	memcpy(tail + 4, &reclen, sizeof(reclen));
	tail[6] = 0;
	tail[7] = EXT4_FT_DIR_CSUM;

	c = ext2_crc32c(ext2_csum_seed(sp), &inum_le, sizeof(inum_le));
	c = ext2_crc32c(c, &gen_le, sizeof(gen_le));
	c = ext2_crc32c(c, raw, sp->s_block_size - DIR_TAIL_SIZE);
	memcpy(tail + 8, &c, sizeof(c));
}

size_t ext2_dir_block_limit(struct super_block *sp)
{
/* Usable length of a directory block: the checksum tail (if any) is reserved. */
	return sp->s_block_size - (ext2_has_csum(sp) ? DIR_TAIL_SIZE : 0);
}

/*===========================================================================*
 *				ext2_xattr_block_csum_set		     *
 *===========================================================================*/
void ext2_xattr_block_csum_set(struct super_block *sp, block64_t blocknr,
	void *block)
{
/* Checksum an extended-attribute block: crc32c of the 64-bit block number then
 * the whole block with the h_checksum field (offset 16) zeroed. */
	u8_t *raw = (u8_t *) block;
	u64_t bnr = blocknr;		/* little-endian on amd64 */
	u32_t zero = 0, c;

	if (!ext2_has_csum(sp))
		return;
	memcpy(raw + 16, &zero, sizeof(zero));
	c = ext2_crc32c(ext2_csum_seed(sp), &bnr, sizeof(bnr));
	c = ext2_crc32c(c, raw, sp->s_block_size);
	memcpy(raw + 16, &c, sizeof(c));
}

/*===========================================================================*
 *				ext2_extent_block_csum_set		     *
 *===========================================================================*/
void ext2_extent_block_csum_set(struct inode *rip, void *block)
{
/* Compute eb_checksum (last 4 bytes) over an on-disk extent-tree node. */
	struct super_block *sp = rip->i_sp;
	u8_t *raw = (u8_t *) block;
	u32_t inum_le = (u32_t) rip->i_num, gen_le = rip->i_generation, c;

	if (!ext2_has_csum(sp))
		return;
	c = ext2_crc32c(ext2_csum_seed(sp), &inum_le, sizeof(inum_le));
	c = ext2_crc32c(c, &gen_le, sizeof(gen_le));
	c = ext2_crc32c(c, raw, sp->s_block_size - EXTENT_TAIL_SIZE);
	memcpy(raw + sp->s_block_size - EXTENT_TAIL_SIZE, &c, sizeof(c));
}
