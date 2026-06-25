#ifndef __MFS_XATTR_H__
#define __MFS_XATTR_H__

/* On-disk format of a per-inode extended-attribute block.
 *
 * A V4 inode's d4_xattr_zone points at a single zone holding all of that
 * inode's extended attributes (gated by MFS_INCOMPAT_XATTR_BLOCK).  The block
 * begins with a small header, followed by packed, 4-byte-aligned entries.  Each
 * entry is a fixed header immediately followed by the attribute name (no NUL
 * stored) and then the value bytes:
 *
 *   [ header ][ entry0 hdr | name0 | value0 | pad ][ entry1 hdr | ... ] ...
 *
 * All attributes for one inode must fit in this single block; a set that would
 * overflow it fails with ENOSPC.
 */

#define MFS_XATTR_MAGIC		0x3141584dUL	/* "MXA1" */

struct mfs_xattr_hdr {
  u32_t xh_magic;		/* MFS_XATTR_MAGIC */
  u32_t xh_count;		/* number of attribute entries that follow */
};

struct mfs_xattr_ent {
  u8_t  xe_ns;			/* namespace (EXTATTR_NAMESPACE_*) */
  u8_t  xe_namelen;		/* name length, 1..MFS_XATTR_NAME_MAX */
  u16_t xe_vallen;		/* value length, 0..MFS_XATTR_VAL_MAX */
  /* followed by: char xe_name[xe_namelen]; char xe_value[xe_vallen]; */
  /* then padding up to the next 4-byte boundary */
};

#define MFS_XATTR_NAME_MAX	255
#define MFS_XATTR_VAL_MAX	65535

/* Size an entry occupies in the block, including its 4-byte alignment. */
#define MFS_XATTR_ENTSIZE(namelen, vallen) \
	(((sizeof(struct mfs_xattr_ent) + (namelen) + (vallen)) + 3) & ~3U)

#endif /* __MFS_XATTR_H__ */
