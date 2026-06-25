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
  u32_t jsb_sequence;		/* sequence to assign to the next transaction;
				 * every transaction with a smaller sequence is
				 * already checkpointed in place */
  u32_t jsb_start;		/* journal-relative block of the next txn (1) */
  u32_t jsb_flags;		/* reserved, must be zero */
};

/* Transaction descriptor block: the home block numbers of the data blocks that
 * immediately follow it in the journal. */
struct mfs_journal_desc {
  u32_t jd_magic;		/* MFS_JDESC_MAGIC */
  u32_t jd_sequence;		/* this transaction's sequence number */
  u32_t jd_count;		/* number of data blocks in the transaction */
  u32_t jd_target[1];		/* jd_count home block numbers (flexible) */
};

/* Commit block: written last, marks the transaction durable. */
struct mfs_journal_commit {
  u32_t jc_magic;		/* MFS_JCOMMIT_MAGIC */
  u32_t jc_sequence;		/* must equal the descriptor's jd_sequence */
  u32_t jc_checksum;		/* CRC32 of the descriptor + all data blocks */
};

/* Maximum home blocks recordable in one descriptor block of 'bs' bytes. */
#define MFS_JDESC_MAX(bs) \
	(((bs) - (unsigned)(3 * sizeof(u32_t))) / (unsigned)sizeof(u32_t))

#endif /* __MFS_JOURNAL_H__ */
