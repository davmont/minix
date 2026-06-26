/* Write support for ext4 extent-mapped inodes.
 *
 * The read side (mapping a logical block to its physical block by walking the
 * on-disk extent tree) lives in read.c; this file maintains that tree when
 * blocks are allocated or freed:
 *
 *   ext4_extent_init_inode:   give a fresh inode an empty extent tree
 *   ext4_extent_insert:       map one newly allocated logical block
 *   ext4_extent_remove_range: free the blocks of a logical-block range
 *
 * The tree root lives in the 60-byte i_block[] area and holds up to four
 * entries; interior/leaf nodes are full disk blocks.  We create and maintain
 * trees of at most depth 1 (root index entries -> on-disk leaves), which on a
 * 4 KiB-block volume already addresses several thousand extents of up to
 * 32768 blocks each -- far more than any non-pathological file needs.  A file
 * that would require a deeper tree (a root index full of full leaves) is
 * refused with EFBIG rather than mis-encoded; this never corrupts on-disk
 * state.  The on-disk format is little-endian and its 16/32-bit fields do not
 * line up with the inode's u32 i_block[] framing, so all access goes through
 * the explicit byte readers/writers below (matching read.c).
 *
 * Created:
 *   June 2026 (ext4 write support)
 */

#include "fs.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "buf.h"
#include "inode.h"
#include "super.h"
#include "const.h"

/* The extent tree root occupies the whole i_block[] area. */
#define EXT_ROOT_BYTES		(EXT2_N_BLOCKS * (int) sizeof(u32_t))	/* 60 */
#define EXT_HDR_SIZE		12	/* extent-tree node header */
#define EXT_ENT_SIZE		12	/* extent or index entry */
#define EXT_ROOT_MAX		((EXT_ROOT_BYTES - EXT_HDR_SIZE) / EXT_ENT_SIZE)
#define EXT_INIT_MAX_LEN	32768	/* ee_len > this marks an uninit extent */

/* Result codes for the in-node leaf insert helper. */
#define INS_DONE	0	/* mapping inserted (or merged) */
#define INS_FULL	1	/* node is full, caller must grow/split */

/* Little-endian accessors for the on-disk extent structures. */
static u16_t ext_le16(const u8_t *p)
{
	return (u16_t)(p[0] | (p[1] << 8));
}
static u32_t ext_le32(const u8_t *p)
{
	return (u32_t) p[0] | ((u32_t) p[1] << 8) | ((u32_t) p[2] << 16) |
	    ((u32_t) p[3] << 24);
}
static void ext_put16(u8_t *p, u16_t v)
{
	p[0] = (u8_t)(v & 0xff);
	p[1] = (u8_t)((v >> 8) & 0xff);
}
static void ext_put32(u8_t *p, u32_t v)
{
	p[0] = (u8_t)(v & 0xff);
	p[1] = (u8_t)((v >> 8) & 0xff);
	p[2] = (u8_t)((v >> 16) & 0xff);
	p[3] = (u8_t)((v >> 24) & 0xff);
}

/* Node header field accessors (header = magic,entries,max,depth,generation). */
static u16_t eh_entries(const u8_t *n) { return ext_le16(n + 2); }
static u16_t eh_depth(const u8_t *n)   { return ext_le16(n + 6); }
static int   eh_valid(const u8_t *n)   { return ext_le16(n) == EXT4_EXT_MAGIC; }

static void set_header(u8_t *n, u16_t entries, u16_t max, u16_t depth)
{
	ext_put16(n + 0, EXT4_EXT_MAGIC);
	ext_put16(n + 2, entries);
	ext_put16(n + 4, max);
	ext_put16(n + 6, depth);
	ext_put32(n + 8, 0);			/* generation */
}
static void set_entries(u8_t *n, u16_t entries) { ext_put16(n + 2, entries); }

/* A decoded leaf extent: 'len' is the real length (1..32768), with the
 * uninitialized flag kept separately. */
struct dext {
	u32_t block;		/* first logical block */
	u32_t len;		/* length in blocks (real, not raw) */
	int uninit;		/* extent is allocated but uninitialized */
	u64_t phys;		/* first physical block */
};

static void get_extent(const u8_t *n, int i, struct dext *x)
{
	const u8_t *e = n + EXT_HDR_SIZE + i * EXT_ENT_SIZE;
	u16_t raw = ext_le16(e + 4);

	x->block = ext_le32(e);
	x->uninit = raw > EXT_INIT_MAX_LEN;
	x->len = x->uninit ? raw - EXT_INIT_MAX_LEN : raw;
	x->phys = ((u64_t) ext_le16(e + 6) << 32) | ext_le32(e + 8);
}
static void put_extent(u8_t *n, int i, const struct dext *x)
{
	u8_t *e = n + EXT_HDR_SIZE + i * EXT_ENT_SIZE;
	u16_t raw = (u16_t)(x->len + (x->uninit ? EXT_INIT_MAX_LEN : 0));

	ext_put32(e + 0, x->block);
	ext_put16(e + 4, raw);
	ext_put16(e + 6, (u16_t)(x->phys >> 32));
	ext_put32(e + 8, (u32_t)(x->phys & 0xffffffff));
}

/* Index-entry accessors (depth > 0): ei_block, ei_leaf_lo, ei_leaf_hi. */
static u32_t idx_block(const u8_t *n, int i)
{
	return ext_le32(n + EXT_HDR_SIZE + i * EXT_ENT_SIZE);
}
static block_t idx_leaf(const u8_t *n, int i)
{
	const u8_t *e = n + EXT_HDR_SIZE + i * EXT_ENT_SIZE;
	return (block_t)(((u64_t) ext_le16(e + 8) << 32) | ext_le32(e + 4));
}
static void put_index(u8_t *n, int i, u32_t block, block_t leaf)
{
	u8_t *e = n + EXT_HDR_SIZE + i * EXT_ENT_SIZE;
	ext_put32(e + 0, block);
	ext_put32(e + 4, (u32_t)((u64_t) leaf & 0xffffffff));
	ext_put16(e + 8, (u16_t)((u64_t) leaf >> 32));
}

/* Leaf max entries for a full disk block of the given size. */
static int block_leaf_max(const struct inode *rip)
{
	return (rip->i_sp->s_block_size - EXT_HDR_SIZE) / EXT_ENT_SIZE;
}

/*===========================================================================*
 *				ext4_extent_init_inode			     *
 *===========================================================================*/
void ext4_extent_init_inode(struct inode *rip)
{
/* Turn a freshly allocated inode into an (empty) extent-mapped inode: install
 * an empty depth-0 extent header in i_block[] and set the on-disk flag.  Only
 * regular files and directories should be given extent trees (symlinks may be
 * "fast" and store their target in i_block[]; device nodes have no blocks). */
	u8_t root[EXT_ROOT_BYTES];

	memset(root, 0, sizeof(root));
	set_header(root, 0, EXT_ROOT_MAX, 0);
	memcpy(rip->i_block, root, sizeof(root));
	rip->i_flags |= EXT4_EXTENTS_FL;
	rip->i_dirt = IN_DIRTY;
}

/*===========================================================================*
 *				leaf_insert				     *
 *===========================================================================*/
static int leaf_insert(u8_t *node, int maxent, u32_t lblock, block_t phys)
{
/* Insert a single mapping (lblock -> phys, length 1) into a leaf node, keeping
 * its extents sorted by logical block.  Merge with the adjacent extent when
 * the mapping is physically and logically contiguous.  Returns INS_DONE on
 * success or INS_FULL when a new entry is needed but the node is full. */
	int entries = eh_entries(node);
	int i, pos;
	struct dext x;

	/* Find the insertion point: first extent whose block is past lblock. */
	pos = entries;
	for (i = 0; i < entries; i++) {
		if (ext_le32(node + EXT_HDR_SIZE + i * EXT_ENT_SIZE) > lblock) {
			pos = i;
			break;
		}
	}

	/* Append to the previous extent if contiguous. */
	if (pos > 0) {
		get_extent(node, pos - 1, &x);
		if (!x.uninit && x.block + x.len == lblock &&
		    x.phys + x.len == (u64_t) phys && x.len < EXT_INIT_MAX_LEN) {
			x.len++;
			put_extent(node, pos - 1, &x);
			return INS_DONE;
		}
		/* lblock already inside the previous extent: nothing to do.
		 * new_block() only calls us for unmapped blocks, so this guards
		 * against an unexpected double insert rather than occurring. */
		if (lblock < x.block + x.len)
			return INS_DONE;
	}

	/* Prepend to the next extent if contiguous. */
	if (pos < entries) {
		get_extent(node, pos, &x);
		if (!x.uninit && lblock + 1 == x.block &&
		    (u64_t) phys + 1 == x.phys && x.len < EXT_INIT_MAX_LEN) {
			x.block = lblock;
			x.phys = phys;
			x.len++;
			put_extent(node, pos, &x);
			return INS_DONE;
		}
	}

	/* A brand-new entry is required. */
	if (entries >= maxent)
		return INS_FULL;

	memmove(node + EXT_HDR_SIZE + (pos + 1) * EXT_ENT_SIZE,
	    node + EXT_HDR_SIZE + pos * EXT_ENT_SIZE,
	    (entries - pos) * EXT_ENT_SIZE);
	x.block = lblock;
	x.len = 1;
	x.uninit = 0;
	x.phys = phys;
	put_extent(node, pos, &x);
	set_entries(node, entries + 1);
	return INS_DONE;
}

/*===========================================================================*
 *				grow_to_depth1				     *
 *===========================================================================*/
static int grow_to_depth1(struct inode *rip, u8_t *root, u32_t lblock,
	block_t phys, int sectors)
{
/* The depth-0 root is full.  Move its extents into a freshly allocated leaf
 * block, insert the new mapping there, and turn the root into a depth-1 node
 * with a single index entry pointing at that leaf. */
	int blockmax = block_leaf_max(rip);
	block_t leafblk;
	struct buf *bp;
	u8_t *leaf;
	u32_t first_block;

	if ((leafblk = alloc_block(rip, rip->i_bsearch)) == NO_BLOCK)
		return(ENOSPC);
	if ((bp = get_block(rip->i_dev, leafblk, NO_READ)) == NULL) {
		free_block(rip->i_sp, leafblk);
		return(EIO);
	}
	leaf = (u8_t *) b_data(bp);
	memset(leaf, 0, rip->i_sp->s_block_size);
	set_header(leaf, EXT_ROOT_MAX, blockmax, 0);
	memcpy(leaf + EXT_HDR_SIZE, root + EXT_HDR_SIZE,
	    EXT_ROOT_MAX * EXT_ENT_SIZE);

	if (leaf_insert(leaf, blockmax, lblock, phys) != INS_DONE) {
		/* Cannot happen: blockmax is far larger than EXT_ROOT_MAX. */
		put_block(bp);
		free_block(rip->i_sp, leafblk);
		return(EFBIG);
	}
	first_block = ext_le32(leaf + EXT_HDR_SIZE);
	ext2_extent_block_csum_set(rip, leaf);
	lmfs_markdirty(bp);
	put_block(bp);

	memset(root, 0, EXT_ROOT_BYTES);
	set_header(root, 1, EXT_ROOT_MAX, 1);
	put_index(root, 0, first_block, leafblk);

	memcpy(rip->i_block, root, EXT_ROOT_BYTES);
	rip->i_blocks += sectors;		/* the new data block */
	rip->i_blocks += sectors;		/* the new leaf block */
	rip->i_dirt = IN_DIRTY;
	return(OK);
}

/*===========================================================================*
 *				insert_depth1				     *
 *===========================================================================*/
static int insert_depth1(struct inode *rip, u8_t *root, u32_t lblock,
	block_t phys, int sectors)
{
/* Insert a mapping into a depth-1 tree: descend to the leaf covering lblock
 * and add the mapping there; if that leaf is full, allocate a new leaf for the
 * mapping and add a root index entry for it (refusing a depth-2 tree). */
	int entries = eh_entries(root);
	int blockmax = block_leaf_max(rip);
	int i, idx, at, r;
	block_t leafblk;
	struct buf *bp;
	u8_t *leaf;

	/* Index entry covering lblock: the last one whose key is <= lblock. */
	idx = 0;
	for (i = 0; i < entries; i++) {
		if (idx_block(root, i) <= lblock)
			idx = i;
		else
			break;
	}
	leafblk = idx_leaf(root, idx);
	if ((bp = get_block(rip->i_dev, leafblk, NORMAL)) == NULL)
		return(EIO);
	leaf = (u8_t *) b_data(bp);
	if (!eh_valid(leaf)) {
		put_block(bp);
		return(EINVAL);
	}

	r = leaf_insert(leaf, blockmax, lblock, phys);
	if (r == INS_DONE) {
		ext2_extent_block_csum_set(rip, leaf);
		lmfs_markdirty(bp);
		put_block(bp);
		/* Keep the index key in sync if we became the first extent. */
		if (lblock < idx_block(root, idx)) {
			ext_put32(root + EXT_HDR_SIZE + idx * EXT_ENT_SIZE,
			    lblock);
			memcpy(rip->i_block, root, EXT_ROOT_BYTES);
		}
		rip->i_blocks += sectors;	/* the new data block */
		rip->i_dirt = IN_DIRTY;
		return(OK);
	}
	put_block(bp);				/* leaf unchanged */

	/* Leaf full: a new leaf and a new root index entry are needed. */
	if (entries >= EXT_ROOT_MAX) {
		ext2_debug("ext4: extent tree would need depth 2 (unsupported)\n");
		return(EFBIG);
	}
	if ((leafblk = alloc_block(rip, rip->i_bsearch)) == NO_BLOCK)
		return(ENOSPC);
	if ((bp = get_block(rip->i_dev, leafblk, NO_READ)) == NULL) {
		free_block(rip->i_sp, leafblk);
		return(EIO);
	}
	leaf = (u8_t *) b_data(bp);
	memset(leaf, 0, rip->i_sp->s_block_size);
	set_header(leaf, 0, blockmax, 0);
	(void) leaf_insert(leaf, blockmax, lblock, phys);	/* always fits */
	ext2_extent_block_csum_set(rip, leaf);
	lmfs_markdirty(bp);
	put_block(bp);

	/* Insert the index entry into the root, keeping it sorted. */
	at = entries;
	for (i = 0; i < entries; i++) {
		if (idx_block(root, i) > lblock) {
			at = i;
			break;
		}
	}
	memmove(root + EXT_HDR_SIZE + (at + 1) * EXT_ENT_SIZE,
	    root + EXT_HDR_SIZE + at * EXT_ENT_SIZE,
	    (entries - at) * EXT_ENT_SIZE);
	put_index(root, at, lblock, leafblk);
	set_entries(root, entries + 1);

	memcpy(rip->i_block, root, EXT_ROOT_BYTES);
	rip->i_blocks += sectors;		/* the new data block */
	rip->i_blocks += sectors;		/* the new leaf block */
	rip->i_dirt = IN_DIRTY;
	return(OK);
}

/*===========================================================================*
 *				ext4_extent_insert			     *
 *===========================================================================*/
int ext4_extent_insert(struct inode *rip, u32_t lblock, block_t phys)
{
/* Map logical block 'lblock' of 'rip' to the just-allocated physical block
 * 'phys' in the inode's extent tree, updating i_blocks for the data block (and
 * for any extent-tree block this allocates).  Called from new_block() in place
 * of write_map() for extent-mapped inodes. */
	u8_t root[EXT_ROOT_BYTES];
	int sectors = rip->i_sp->s_sectors_in_block;
	u16_t depth;

	memcpy(root, rip->i_block, sizeof(root));
	if (!eh_valid(root))
		return(EINVAL);
	depth = eh_depth(root);

	if (depth == 0) {
		if (leaf_insert(root, EXT_ROOT_MAX, lblock, phys) == INS_DONE) {
			memcpy(rip->i_block, root, sizeof(root));
			rip->i_blocks += sectors;
			rip->i_dirt = IN_DIRTY;
			return(OK);
		}
		return grow_to_depth1(rip, root, lblock, phys, sectors);
	} else if (depth == 1) {
		return insert_depth1(rip, root, lblock, phys, sectors);
	}

	ext2_debug("ext4: unsupported extent tree depth %u\n", depth);
	return(EFBIG);
}

/*===========================================================================*
 *				leaf_edit				     *
 *===========================================================================*/
static int leaf_edit(struct inode *rip, u8_t *node, u32_t first, u32_t last,
	int commit, int *new_count)
{
/* Remove the logical-block range [first, last) from a leaf node.  With
 * commit == 0 only the resulting entry count is computed (so the caller can
 * detect a split overflow before changing anything); with commit == 1 the
 * overlapping data blocks are freed (updating i_blocks) and the surviving
 * extents are written back.  An extent straddling the range survives as up to
 * two pieces (head and tail), which is the only way the entry count can grow. */
	int entries = eh_entries(node);
	int sectors = rip->i_sp->s_sectors_in_block;
	struct dext *out = NULL;
	int nout = 0, i;

	if (commit) {
		/* +2 headroom: a single extent can split into head and tail. */
		out = malloc(sizeof(struct dext) * (entries + 2));
		if (out == NULL)
			return(ENOSPC);
	}

	for (i = 0; i < entries; i++) {
		struct dext e;
		u32_t estart, eend, lh, rl, flo, fhi;

		get_extent(node, i, &e);
		estart = e.block;
		eend = e.block + e.len;
		lh = (eend < first) ? eend : first;	/* min(eend, first) */
		rl = (estart > last) ? estart : last;	/* max(estart, last) */
		flo = (estart > first) ? estart : first;/* max(estart, first) */
		fhi = (eend < last) ? eend : last;	/* min(eend, last) */

		/* Surviving head [estart, lh). */
		if (estart < lh) {
			if (commit) {
				struct dext h = e;
				h.len = lh - estart;
				out[nout] = h;
			}
			nout++;
		}
		/* Freed middle [flo, fhi). */
		if (commit && flo < fhi) {
			u64_t pbase = e.phys + (flo - estart);
			u32_t b;
			for (b = 0; b < fhi - flo; b++) {
				free_block(rip->i_sp, (bit_t)(pbase + b));
				rip->i_blocks -= sectors;
			}
		}
		/* Surviving tail [rl, eend). */
		if (rl < eend) {
			if (commit) {
				struct dext t = e;
				t.block = rl;
				t.len = eend - rl;
				t.phys = e.phys + (rl - estart);
				out[nout] = t;
			}
			nout++;
		}
	}

	if (commit) {
		for (i = 0; i < nout; i++)
			put_extent(node, i, &out[i]);
		set_entries(node, nout);
		free(out);
	}
	if (new_count != NULL)
		*new_count = nout;
	return(OK);
}

/*===========================================================================*
 *				ext4_extent_remove_range		     *
 *===========================================================================*/
int ext4_extent_remove_range(struct inode *rip, u32_t first, u32_t last)
{
/* Free every block mapped to a logical block in [first, last) and update the
 * extent tree (and i_blocks) accordingly.  Used by the truncate/F_FREESP path
 * for extent-mapped inodes in place of the write_map(WMAP_FREE) loop.  For a
 * tail truncate (the common case, last == end-of-file) no extent ever splits,
 * so the EFBIG overflow path below is only reachable for an interior hole
 * punch that would split a full node. */
	u8_t root[EXT_ROOT_BYTES];
	int sectors = rip->i_sp->s_sectors_in_block;
	u16_t depth;
	int cnt, r;

	if (last <= first)
		return(OK);

	memcpy(root, rip->i_block, sizeof(root));
	if (!eh_valid(root))
		return(EINVAL);
	depth = eh_depth(root);

	if (depth == 0) {
		if ((r = leaf_edit(rip, root, first, last, 0, &cnt)) != OK)
			return(r);
		if (cnt > EXT_ROOT_MAX)
			return(EFBIG);
		if ((r = leaf_edit(rip, root, first, last, 1, &cnt)) != OK)
			return(r);
		memcpy(rip->i_block, root, sizeof(root));
		rip->i_dirt = IN_DIRTY;
		return(OK);
	} else if (depth == 1) {
		struct { u32_t block; block_t leaf; } keep[EXT_ROOT_MAX];
		int entries = eh_entries(root);
		int nkeep = 0;
		int i;

		for (i = 0; i < entries; i++) {
			block_t leafblk = idx_leaf(root, i);
			struct buf *bp;
			u8_t *leaf;
			u32_t fb;

			if ((bp = get_block(rip->i_dev, leafblk, NORMAL)) == NULL)
				return(EIO);
			leaf = (u8_t *) b_data(bp);
			if (!eh_valid(leaf)) {
				put_block(bp);
				return(EINVAL);
			}
			leaf_edit(rip, leaf, first, last, 0, &cnt);
			if (cnt > block_leaf_max(rip)) {
				put_block(bp);
				return(EFBIG);
			}
			leaf_edit(rip, leaf, first, last, 1, &cnt);
			if (cnt == 0) {
				/* Leaf emptied: free it and drop its index. */
				put_block(bp);
				free_block(rip->i_sp, leafblk);
				rip->i_blocks -= sectors;
			} else {
				fb = ext_le32(leaf + EXT_HDR_SIZE);
				ext2_extent_block_csum_set(rip, leaf);
				lmfs_markdirty(bp);
				put_block(bp);
				keep[nkeep].block = fb;
				keep[nkeep].leaf = leafblk;
				nkeep++;
			}
		}

		if (nkeep == 0) {
			/* Whole tree gone: collapse to an empty depth-0 root. */
			memset(root, 0, sizeof(root));
			set_header(root, 0, EXT_ROOT_MAX, 0);
		} else if (nkeep == 1) {
			/* A single leaf survives.  If its extents now fit in the
			 * inode root, pull them up and drop to a depth-0 tree,
			 * freeing the leaf block, so the tree is kept no deeper
			 * than necessary (what e2fsck's -D pass would do). */
			block_t leafblk = keep[0].leaf;
			struct buf *bp;
			u8_t *leaf;
			int lent;

			if ((bp = get_block(rip->i_dev, leafblk, NORMAL)) == NULL)
				return(EIO);
			leaf = (u8_t *) b_data(bp);
			lent = eh_entries(leaf);
			if (eh_valid(leaf) && lent <= EXT_ROOT_MAX) {
				memset(root, 0, sizeof(root));
				set_header(root, (u16_t) lent, EXT_ROOT_MAX, 0);
				memcpy(root + EXT_HDR_SIZE, leaf + EXT_HDR_SIZE,
				    lent * EXT_ENT_SIZE);
				put_block(bp);
				free_block(rip->i_sp, leafblk);
				rip->i_blocks -= sectors;
			} else {
				/* Too many extents to inline: keep depth 1. */
				put_block(bp);
				set_header(root, 1, EXT_ROOT_MAX, 1);
				put_index(root, 0, keep[0].block, keep[0].leaf);
			}
		} else {
			set_header(root, nkeep, EXT_ROOT_MAX, 1);
			for (i = 0; i < nkeep; i++)
				put_index(root, i, keep[i].block, keep[i].leaf);
		}
		memcpy(rip->i_block, root, sizeof(root));
		rip->i_dirt = IN_DIRTY;
		return(OK);
	}

	ext2_debug("ext4: unsupported extent tree depth %u\n", depth);
	return(EFBIG);
}
