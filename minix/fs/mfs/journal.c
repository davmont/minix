/* MFS metadata journal — the log engine (see JOURNAL_DESIGN.md).
 *
 * A physical redo log with write-ahead logging.  A transaction is the set of
 * metadata blocks dirtied by one filesystem request; MARKDIRTY() records each
 * such block here via journal_track().  At the end of a modifying request
 * journal_commit() writes the transaction to the journal with direct, ordered
 * device I/O (descriptor + data blocks + commit record), and only once that is
 * durable does it write the blocks to their home locations (the lmfs cache
 * flush) and advance the journal.  On mount, journal_recover() replays a
 * committed-but-not-checkpointed transaction, so a crash never leaves a torn
 * in-place update.  Replay is idempotent.
 *
 * The journal holds at most one transaction at a time (each request commits
 * and checkpoints before the next runs), so transactions are written starting
 * at the first journal block after the journal superblock, and the superblock's
 * jsb_sequence records the sequence to assign next: a transaction whose
 * sequence equals jsb_sequence is the pending one to replay.
 */

#include "fs.h"
#include "buf.h"
#include "inode.h"
#include "super.h"
#include "journal.h"
#include <minix/bdev.h>
#include <stdlib.h>
#include <string.h>

static struct {
  int active;			/* journaling on (journalled FS, mounted r/w) */
  dev_t dev;
  u32_t jstart;			/* device block of the journal superblock */
  u32_t jblocks;		/* journal length in blocks */
  unsigned bsize;		/* filesystem block size */
  u32_t sequence;		/* sequence to assign to the next transaction */
  u32_t *txn;			/* home block numbers in the current txn */
  unsigned txn_count;
  unsigned txn_max;		/* capacity == max blocks a txn may hold */
  u32_t *dtxn;			/* file-data home blocks to flush ordered */
  unsigned dtxn_count;
  unsigned dtxn_max;		/* capacity; flushed eagerly when full */
  char *iobuf;			/* one block of scratch for device I/O */
  int crash_skip;		/* test hook: commit to journal, do not checkpoint */
  int crashed;			/* test hook: latch -- suppress all checkpointing */
} jr;

/*===========================================================================*
 *				crc32					     *
 *===========================================================================*/
static u32_t crc32(u32_t crc, const void *buf, size_t len)
{
/* Standard CRC-32 (reflected, polynomial 0xEDB88320), computed without a
 * table.  Used to detect a torn journal transaction during recovery. */
  const unsigned char *p = buf;
  unsigned int k;

  crc = ~crc;
  while (len--) {
	crc ^= *p++;
	for (k = 0; k < 8; k++)
		crc = (crc >> 1) ^ (0xEDB88320UL & (~(crc & 1) + 1));
  }
  return ~crc;
}

/*===========================================================================*
 *				jdev_write				     *
 *===========================================================================*/
static int jdev_write(u32_t block, const void *buf)
{
/* Write one block straight to the device, bypassing the cache, so the journal
 * is ordered with respect to the home (cache) writes. */
  ssize_t r;

  r = bdev_write(jr.dev, (u64_t) block * jr.bsize, __UNCONST(buf), jr.bsize,
	BDEV_NOFLAGS);
  if (r != (ssize_t) jr.bsize) {
	printf("MFS: journal device write at block %u failed: %d\n", block,
		(int) r);
	return(EIO);
  }
  return(OK);
}

/*===========================================================================*
 *				jdev_read				     *
 *===========================================================================*/
static int jdev_read(u32_t block, void *buf)
{
  ssize_t r;

  r = bdev_read(jr.dev, (u64_t) block * jr.bsize, buf, jr.bsize, BDEV_NOFLAGS);
  if (r != (ssize_t) jr.bsize) {
	printf("MFS: journal device read at block %u failed: %d\n", block,
		(int) r);
	return(EIO);
  }
  return(OK);
}

/*===========================================================================*
 *				journal_recover				     *
 *===========================================================================*/
int journal_recover(struct super_block *sp, dev_t dev)
{
/* At mount time, replay a committed-but-not-checkpointed transaction, if any.
 * Uses direct device I/O only; the cache is not yet in use.  Idempotent. */
  struct mfs_journal_super *jsb;
  struct mfs_journal_desc *desc;
  struct mfs_journal_commit *commit;
  char *jsbuf, *dbuf, *cbuf, *blkbuf;
  u32_t start, blocks, seq, n, i, crc;
  unsigned bsize;
  int r = OK;

  if (!(sp->s_feature_incompat & MFS_INCOMPAT_JOURNAL))
	return(OK);

  start = sp->s_journal_start;
  blocks = sp->s_journal_blocks;
  bsize = sp->s_block_size;

  jr.dev = dev;
  jr.jstart = start;
  jr.jblocks = blocks;
  jr.bsize = bsize;

  jsbuf = malloc(bsize);
  dbuf = malloc(bsize);
  cbuf = malloc(bsize);
  blkbuf = malloc(bsize);
  if (!jsbuf || !dbuf || !cbuf || !blkbuf) { r = ENOMEM; goto out; }

  if ((r = jdev_read(start, jsbuf)) != OK) goto out;
  jsb = (struct mfs_journal_super *) jsbuf;
  if (jsb->jsb_magic != MFS_JOURNAL_MAGIC) {
	printf("MFS: bad journal magic; not recovering\n");
	r = OK;			/* leave the FS to the clean-flag path */
	jr.sequence = 0;
	goto out;
  }
  seq = jsb->jsb_sequence;
  jr.sequence = seq;

  /* The pending transaction, if any, is the one at the first journal block
   * whose descriptor carries the not-yet-checkpointed sequence number. */
  if ((r = jdev_read(start + 1, dbuf)) != OK) goto out;
  desc = (struct mfs_journal_desc *) dbuf;
  if (desc->jd_magic != MFS_JDESC_MAGIC || desc->jd_sequence != seq)
	goto out;		/* nothing pending */
  n = desc->jd_count;
  if (n < 1 || n + 2 > blocks - 1 || n > MFS_JDESC_MAX(bsize))
	goto out;		/* implausible: ignore */

  /* Verify the commit record and the checksum over descriptor + data. */
  if ((r = jdev_read(start + 2 + n, cbuf)) != OK) goto out;
  commit = (struct mfs_journal_commit *) cbuf;
  crc = crc32(0, dbuf, bsize);
  for (i = 0; i < n; i++) {
	if ((r = jdev_read(start + 2 + i, blkbuf)) != OK) goto out;
	crc = crc32(crc, blkbuf, bsize);
  }
  if (commit->jc_magic != MFS_JCOMMIT_MAGIC ||
      commit->jc_sequence != seq || commit->jc_checksum != crc) {
	printf("MFS: incomplete journal transaction %u; discarding\n", seq);
	goto out;		/* torn tail: discard */
  }

  /* Replay: write each data block to its home location. */
  printf("MFS: recovering journal: replaying transaction %u (%u blocks)\n",
	seq, n);
  for (i = 0; i < n; i++) {
	if ((r = jdev_read(start + 2 + i, blkbuf)) != OK) goto out;
	if ((r = jdev_write(desc->jd_target[i], blkbuf)) != OK) goto out;
  }

  /* Mark the transaction checkpointed by advancing the journal superblock. */
  jsb->jsb_sequence = seq + 1;
  jsb->jsb_start = 1;
  if ((r = jdev_write(start, jsbuf)) != OK) goto out;
  jr.sequence = seq + 1;

out:
  free(jsbuf); free(dbuf); free(cbuf); free(blkbuf);
  return(r);
}

/*===========================================================================*
 *				journal_init				     *
 *===========================================================================*/
int journal_init(struct super_block *sp, dev_t dev)
{
/* Activate journaling for a read/write journalled mount.  journal_recover()
 * must already have run (it set jr.sequence). */
  if (!(sp->s_feature_incompat & MFS_INCOMPAT_JOURNAL) || sp->s_rd_only)
	return(OK);

  jr.dev = dev;
  jr.jstart = sp->s_journal_start;
  jr.jblocks = sp->s_journal_blocks;
  jr.bsize = sp->s_block_size;

  jr.txn_max = MFS_JDESC_MAX(jr.bsize);
  if (jr.txn_max > jr.jblocks - 3) jr.txn_max = jr.jblocks - 3;
  jr.dtxn_max = 256;		/* ordered-data flush buffer (bounded) */
  jr.txn = malloc(jr.txn_max * sizeof(u32_t));
  jr.dtxn = malloc(jr.dtxn_max * sizeof(u32_t));
  jr.iobuf = malloc(jr.bsize);
  if (jr.txn == NULL || jr.dtxn == NULL || jr.iobuf == NULL) {
	free(jr.txn); free(jr.dtxn); free(jr.iobuf);
	jr.txn = NULL; jr.dtxn = NULL; jr.iobuf = NULL;
	printf("MFS: cannot allocate journal buffers; journaling disabled\n");
	return(OK);		/* degrade to the clean-flag scheme */
  }
  jr.txn_count = 0;
  jr.dtxn_count = 0;
  jr.crash_skip = 0;
  jr.active = 1;
  return(OK);
}

/*===========================================================================*
 *				journal_stop				     *
 *===========================================================================*/
void journal_stop(void)
{
/* Commit any pending work and release journal resources (at unmount). */
  if (!jr.active) return;
  (void) journal_commit();
  free(jr.txn);
  free(jr.dtxn);
  free(jr.iobuf);
  jr.txn = NULL;
  jr.dtxn = NULL;
  jr.iobuf = NULL;
  jr.active = 0;
}

/*===========================================================================*
 *				journal_track				     *
 *===========================================================================*/
void journal_track(u32_t block)
{
/* Record that a metadata block was dirtied as part of the current request.
 * Duplicates (a block dirtied more than once) are folded together. */
  unsigned i;

  if (!jr.active) return;

  for (i = 0; i < jr.txn_count; i++)
	if (jr.txn[i] == block) return;

  if (jr.txn_count < jr.txn_max)
	jr.txn[jr.txn_count++] = block;
  else
	jr.txn_count = jr.txn_max + 1;	/* overflow marker: handled at commit */
}

/*===========================================================================*
 *				flush_data				     *
 *===========================================================================*/
static void flush_data(void)
{
/* data=ordered: write the file-data blocks recorded for this transaction to
 * their home locations and mark them clean, so committed metadata never points
 * at unwritten data and the later metadata checkpoint need not rewrite them.
 * Data is never journalled.  Called before the metadata commit, and eagerly
 * when the data list fills (flushing early is always safe for ordering). */
  struct buf *bp;
  unsigned i;
  int r;

  for (i = 0; i < jr.dtxn_count; i++) {
	if ((r = lmfs_get_block(&bp, jr.dev, jr.dtxn[i], NORMAL)) != OK) {
		printf("MFS: journal: cannot read data block %u: %d\n",
			jr.dtxn[i], r);
		continue;
	}
	if (jdev_write(jr.dtxn[i], b_data(bp)) == OK)
		lmfs_markclean(bp);
	put_block(bp);
  }
  jr.dtxn_count = 0;
}

/*===========================================================================*
 *				journal_track_data			     *
 *===========================================================================*/
void journal_track_data(u32_t block)
{
/* Record that a regular-file data block was dirtied as part of the current
 * request.  Data is not journalled; it is flushed to its home location before
 * the metadata commit (see flush_data()).  Duplicates are folded together; the
 * buffer is flushed eagerly when it fills, to bound memory for large writes. */
  unsigned i;

  if (!jr.active) return;

  for (i = 0; i < jr.dtxn_count; i++)
	if (jr.dtxn[i] == block) return;

  if (jr.dtxn_count == jr.dtxn_max)
	flush_data();			/* full: flush early (still pre-commit) */

  jr.dtxn[jr.dtxn_count++] = block;
}

/*===========================================================================*
 *				journal_crash_test			     *
 *===========================================================================*/
void journal_crash_test(void)
{
/* Test hook: make the next commit write the transaction to the journal but
 * skip the in-place checkpoint and the journal advance, simulating a crash
 * right after commit.  Also latch 'crashed', so that EVERY subsequent flush
 * (including the periodic update/sync) is suppressed too: this models a machine
 * that is dead after the commit, guaranteeing the home blocks stay stale so the
 * only way the change can survive is journal replay at the next mount. */
  if (jr.active) { jr.crash_skip = 1; jr.crashed = 1; }
}

/*===========================================================================*
 *				journal_crashed				     *
 *===========================================================================*/
int journal_crashed(void)
{
/* Test hook: report whether the crash latch is set, so fs_sync() can refrain
 * from checkpointing the cache to disk after a simulated crash. */
  return jr.crashed;
}

/*===========================================================================*
 *				journal_commit				     *
 *===========================================================================*/
int journal_commit(void)
{
/* Commit the current transaction: write it to the journal (durable), then
 * write its blocks to their home locations, then advance the journal. */
  struct inode *rip;
  struct buf *bp;
  struct mfs_journal_desc *desc;
  struct mfs_journal_commit *commit;
  struct mfs_journal_super *jsb;
  u32_t seq, crc, n, i, base;
  int r;

  if (!jr.active) return(OK);

  /* Flush dirty in-core inodes into their cache blocks; this MARKDIRTYs (and
   * so journal_track()s) the inode blocks. */
  for (rip = &inode[0]; rip < &inode[NR_INODES]; rip++)
	if (rip->i_count > 0 && IN_ISDIRTY(rip)) rw_inode(rip, WRITING);

  /* data=ordered: force this transaction's file data to its home location
   * before the metadata is committed, so committed metadata never references
   * data that a crash left unwritten. */
  flush_data();

  if (jr.txn_count == 0) return(OK);

  /* A transaction larger than this single-descriptor journal can hold is not
   * journalled (Phase 2 adds multi-descriptor transactions); the clean flag
   * still guards a crash during such a rare, large operation. */
  if (jr.txn_count > jr.txn_max) {
	printf("MFS: journal transaction too large; flushing unjournalled\n");
	lmfs_flushall();
	jr.txn_count = 0;
	jr.crash_skip = 0;
	return(OK);
  }

  n = jr.txn_count;
  seq = jr.sequence;
  base = jr.jstart + 1;		/* descriptor block */

  /* 1. Descriptor block. */
  memset(jr.iobuf, 0, jr.bsize);
  desc = (struct mfs_journal_desc *) jr.iobuf;
  desc->jd_magic = MFS_JDESC_MAGIC;
  desc->jd_sequence = seq;
  desc->jd_count = n;
  for (i = 0; i < n; i++) desc->jd_target[i] = jr.txn[i];
  crc = crc32(0, jr.iobuf, jr.bsize);
  if ((r = jdev_write(base, jr.iobuf)) != OK) return(r);

  /* 2. Data blocks (the new contents, read from the cache). */
  for (i = 0; i < n; i++) {
	if ((r = lmfs_get_block(&bp, jr.dev, jr.txn[i], NORMAL)) != OK) {
		printf("MFS: journal: cannot read block %u: %d\n", jr.txn[i], r);
		return(r);
	}
	crc = crc32(crc, b_data(bp), jr.bsize);
	r = jdev_write(base + 1 + i, b_data(bp));
	put_block(bp);
	if (r != OK) return(r);
  }

  /* 3. Commit block -- the journal is durable once this returns. */
  memset(jr.iobuf, 0, jr.bsize);
  commit = (struct mfs_journal_commit *) jr.iobuf;
  commit->jc_magic = MFS_JCOMMIT_MAGIC;
  commit->jc_sequence = seq;
  commit->jc_checksum = crc;
  if ((r = jdev_write(base + 1 + n, jr.iobuf)) != OK) return(r);

  if (!jr.crash_skip && !jr.crashed) {
	/* 4. Checkpoint: write the home blocks in place. */
	lmfs_flushall();

	/* 5. Advance: every transaction up to 'seq' is now checkpointed. */
	memset(jr.iobuf, 0, jr.bsize);
	jsb = (struct mfs_journal_super *) jr.iobuf;
	jsb->jsb_magic = MFS_JOURNAL_MAGIC;
	jsb->jsb_blocks = jr.jblocks;
	jsb->jsb_sequence = seq + 1;
	jsb->jsb_start = 1;
	if ((r = jdev_write(jr.jstart, jr.iobuf)) != OK) return(r);
  }

  jr.sequence = seq + 1;
  jr.txn_count = 0;
  jr.crash_skip = 0;
  return(OK);
}
