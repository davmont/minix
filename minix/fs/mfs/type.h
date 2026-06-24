#ifndef __MFS_TYPE_H__
#define __MFS_TYPE_H__

#include <minix/libminixfs.h>

/* Declaration of the V2 inode as it is on the disk (not in core). */
typedef struct {		/* V2.x disk inode */
  u16_t d2_mode;		/* file type, protection, etc. */
  u16_t d2_nlinks;		/* how many links to this file. HACK! */
  i16_t d2_uid;			/* user id of the file's owner. */
  u16_t d2_gid;			/* group number HACK! */
  i32_t d2_size;		/* current file size in bytes */
  i32_t d2_atime;		/* when was file data last accessed */
  i32_t d2_mtime;		/* when was file data last changed */
  i32_t d2_ctime;		/* when was inode data last changed */
  zone_t d2_zone[V2_NR_TZONES];	/* block nums for direct, ind, and dbl ind */
} d2_inode;

/* Declaration of the V4 (MFS4) inode as it is on the disk (128 bytes).  Used
 * when the filesystem has the MFS_INCOMPAT_WIDE_INODE feature.  Fixes V2/V3's
 * 32-bit timestamps (Y2038), 32-bit size, and 16-bit nlinks/gid, and adds an
 * inode-flags field and a birth time.  All fields naturally aligned; see
 * MFSV4_DESIGN.md for the byte layout.
 */
typedef struct {		/* V4 disk inode, 128 bytes */
  u64_t d4_size;		/* current file size in bytes */
  i64_t d4_atime;		/* when was file data last accessed */
  i64_t d4_mtime;		/* when was file data last changed */
  i64_t d4_ctime;		/* when was inode data last changed */
  i64_t d4_crtime;		/* when was the file created (birth time) */
  u16_t d4_mode;		/* file type, protection, etc. */
  u16_t d4_pad0;		/* padding; must be zero */
  u32_t d4_nlinks;		/* how many links to this file */
  u32_t d4_uid;			/* user id of the file's owner */
  u32_t d4_gid;			/* group number */
  u32_t d4_flags;		/* inode flags (chflags: immutable, ...) */
  u32_t d4_xattr_zone;		/* zone holding xattrs, 0 = none (future) */
  zone_t d4_zone[V2_NR_TZONES];	/* block nums for direct, ind, and dbl ind */
  u32_t d4_reserved[6];		/* reserved; must be zero */
} d4_inode;

#endif

