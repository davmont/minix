#ifndef __MFS_JOURNAL_H__
#define __MFS_JOURNAL_H__

/* On-disk format for the MFS metadata journal (the MFS_INCOMPAT_JOURNAL V4
 * feature).  See JOURNAL_DESIGN.md.  The journal is a contiguous run of blocks
 * whose location is recorded in the superblock; block 0 of that run is the
 * journal superblock below.  Phase 0 defines and creates this format; the log
 * engine and recovery that use it are added in a later phase.
 */

#define MFS_JOURNAL_MAGIC	0x314c4a4dUL	/* "MJL1" */
#define MFS_JDESC_MAGIC		0x314a444dUL	/* "MDJ1" descriptor block */
#define MFS_JCOMMIT_MAGIC	0x314a434dUL	/* "MCJ1" commit block */

/* Journal superblock (block 0 of the journal run). */
struct mfs_journal_super {
  u32_t jsb_magic;		/* MFS_JOURNAL_MAGIC */
  u32_t jsb_blocks;		/* total journal blocks, including this one */
  u32_t jsb_sequence;		/* sequence number of the oldest valid txn */
  u32_t jsb_start;		/* block offset within the journal of that txn */
  u32_t jsb_flags;		/* reserved, must be zero */
};

#endif /* __MFS_JOURNAL_H__ */
