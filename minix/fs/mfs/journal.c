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
  unsigned batch_ops;		/* requests accumulated since the last commit */
  unsigned commit_blocks;	/* commit once the running txn reaches this many */
  unsigned commit_ops;		/* ... or this many requests (durability bound) */
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
 *				jdesc_max				     *
 *===========================================================================*/
static unsigned jdesc_max(void)
{
/* Home block numbers recordable in one descriptor block.  A small value can be
 * forced in a debug build to exercise multi-descriptor transactions without a
 * huge operation. */
#ifdef JOURNAL_SMALL_DESC
  return(JOURNAL_SMALL_DESC);
#else
  return(MFS_JDESC_MAX(jr.bsize));
#endif
}

/*===========================================================================*
 *				journal_capacity			     *
 *===========================================================================*/
static unsigned journal_capacity(void)
{
/* The largest number of metadata blocks a single transaction may hold: it needs
 * one descriptor per jdesc_max() blocks plus a commit block, all within the
 * journal (jblocks, the first of which is the journal superblock). */
  unsigned jdmax = jdesc_max();
  unsigned avail, k;

  if (jr.jblocks < 4) return(1);
  avail = jr.jblocks - 2;		/* minus the superblock and commit block */
  k = avail;
  while (k > 1 && k + (k + jdmax - 1) / jdmax > avail) k--;
#ifdef JOURNAL_SMALL_CAP
  if (k > JOURNAL_SMALL_CAP) k = JOURNAL_SMALL_CAP;
#endif
  return(k ? k : 1);
}

/*===========================================================================*
 *				write_txn				     *
 *===========================================================================*/
static int write_txn(unsigned lo, unsigned hi, u32_t seq)
{
/* Write metadata blocks jr.txn[lo..hi) to the journal as one transaction:
 * a run of (descriptor block, its data blocks) groups followed by a single
 * commit block.  A descriptor holds up to jdesc_max() home block numbers, so a
 * transaction with more blocks than that spans several descriptors.  The commit
 * checksum covers every descriptor and data block, in journal order. */
  struct mfs_journal_desc *desc;
  struct mfs_journal_commit *commit;
  struct buf *bp;
  u32_t pos = jr.jstart + 1;
  u32_t crc = 0;
  unsigned jdmax = jdesc_max();
  unsigned i, k, cnt;
  int r;

  for (i = lo; i < hi; i += cnt) {
	cnt = hi - i;
	if (cnt > jdmax) cnt = jdmax;

	/* descriptor */
	memset(jr.iobuf, 0, jr.bsize);
	desc = (struct mfs_journal_desc *) jr.iobuf;
	desc->jd_magic = MFS_JDESC_MAGIC;
	desc->jd_sequence = seq;
	desc->jd_count = cnt;
	for (k = 0; k < cnt; k++) desc->jd_target[k] = jr.txn[i + k];
	crc = crc32(crc, jr.iobuf, jr.bsize);
	if ((r = jdev_write(pos++, jr.iobuf)) != OK) return(r);

	/* the data blocks this descriptor refers to */
	for (k = 0; k < cnt; k++) {
		if ((r = lmfs_get_block(&bp, jr.dev, jr.txn[i+k], NORMAL)) != OK) {
			printf("MFS: journal: cannot read block %u: %d\n",
				jr.txn[i+k], r);
			return(r);
		}
		crc = crc32(crc, b_data(bp), jr.bsize);
		r = jdev_write(pos++, b_data(bp));
		put_block(bp);
		if (r != OK) return(r);
	}
  }

  /* commit block -- the transaction is durable once this returns */
  memset(jr.iobuf, 0, jr.bsize);
  commit = (struct mfs_journal_commit *) jr.iobuf;
  commit->jc_magic = MFS_JCOMMIT_MAGIC;
  commit->jc_sequence = seq;
  commit->jc_checksum = crc;
  return(jdev_write(pos, jr.iobuf));
}

/*===========================================================================*
 *				checkpoint_blocks			     *
 *===========================================================================*/
static int checkpoint_blocks(unsigned lo, unsigned hi)
{
/* Write metadata blocks jr.txn[lo..hi) to their home locations and mark them
 * clean.  Used to checkpoint a chunk of an over-capacity transaction in place
 * (the common, single-chunk case uses lmfs_flushall() instead). */
  struct buf *bp;
  unsigned j;
  int r;

  for (j = lo; j < hi; j++) {
	if ((r = lmfs_get_block(&bp, jr.dev, jr.txn[j], NORMAL)) != OK) return(r);
	r = jdev_write(jr.txn[j], b_data(bp));
	if (r == OK) lmfs_markclean(bp);
	put_block(bp);
	if (r != OK) return(r);
  }
  return(OK);
}

/*===========================================================================*
 *				journal_advance				     *
 *===========================================================================*/
static int journal_advance(u32_t newseq)
{
/* Rewrite the journal superblock so every transaction with a smaller sequence
 * is now considered checkpointed. */
  struct mfs_journal_super *jsb;

  memset(jr.iobuf, 0, jr.bsize);
  jsb = (struct mfs_journal_super *) jr.iobuf;
  jsb->jsb_magic = MFS_JOURNAL_MAGIC;
  jsb->jsb_blocks = jr.jblocks;
  jsb->jsb_sequence = newseq;
  jsb->jsb_start = 1;
  return(jdev_write(jr.jstart, jr.iobuf));
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
  char *jsbuf = NULL, *dbuf = NULL, *blkbuf = NULL;
  u32_t *targets = NULL, *positions = NULL;
  u32_t start, blocks, seq, n, i, crc, pos, cnt;
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
  blkbuf = malloc(bsize);
  /* targets[]/positions[] map each journalled data block to its home location
   * and to the journal block holding its contents; at most 'blocks' of them. */
  targets = malloc(blocks * sizeof(u32_t));
  positions = malloc(blocks * sizeof(u32_t));
  if (!jsbuf || !dbuf || !blkbuf || !targets || !positions) {
	r = ENOMEM; goto out;
  }

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

  /* Walk the pending transaction, if any: a run of (descriptor, its data
   * blocks) groups starting at the first journal block, all carrying the
   * not-yet-checkpointed sequence number, terminated by a commit block.
   * Accumulate the checksum over every descriptor and data block in order, and
   * remember each data block's home target and journal position for replay. */
  pos = start + 1;
  n = 0;
  crc = 0;
  for (;;) {
	if (pos >= start + blocks) goto discard;	/* ran off the end: torn */
	if ((r = jdev_read(pos, dbuf)) != OK) goto out;
	desc = (struct mfs_journal_desc *) dbuf;
	if (desc->jd_magic != MFS_JDESC_MAGIC || desc->jd_sequence != seq)
		break;			/* end of descriptors (commit or nothing) */
	cnt = desc->jd_count;
	if (cnt < 1 || cnt > MFS_JDESC_MAX(bsize)) goto discard;
	crc = crc32(crc, dbuf, bsize);
	pos++;
	for (i = 0; i < cnt; i++) {
		if (n >= blocks || pos >= start + blocks) goto discard;
		if ((r = jdev_read(pos, blkbuf)) != OK) goto out;
		crc = crc32(crc, blkbuf, bsize);
		targets[n] = desc->jd_target[i];
		positions[n] = pos;
		n++;
		pos++;
	}
  }

  if (n == 0) goto out;		/* nothing pending */

  /* The block that ended the descriptor walk (still in dbuf) must be the
   * commit block, matching the sequence and the accumulated checksum. */
  commit = (struct mfs_journal_commit *) dbuf;
  if (commit->jc_magic != MFS_JCOMMIT_MAGIC ||
      commit->jc_sequence != seq || commit->jc_checksum != crc)
	goto discard;		/* torn tail */

  /* Replay: write each journalled data block to its home location. */
  printf("MFS: recovering journal: replaying transaction %u (%u blocks)\n",
	seq, n);
  for (i = 0; i < n; i++) {
	if ((r = jdev_read(positions[i], blkbuf)) != OK) goto out;
	if ((r = jdev_write(targets[i], blkbuf)) != OK) goto out;
  }

  /* Mark the transaction checkpointed by advancing the journal superblock. */
  jsb->jsb_sequence = seq + 1;
  jsb->jsb_start = 1;
  if ((r = jdev_write(start, jsbuf)) != OK) goto out;
  jr.sequence = seq + 1;
  goto out;

discard:
  printf("MFS: incomplete journal transaction %u; discarding\n", seq);
  r = OK;

out:
  free(jsbuf); free(dbuf); free(blkbuf);
  free(targets); free(positions);
  return(r);
}

/*===========================================================================*
 *				journal_init				     *
 *===========================================================================*/
int journal_init(struct super_block *sp, dev_t dev)
{
/* Activate journaling for a read/write journalled mount.  journal_recover()
 * must already have run (it set jr.sequence). */
  unsigned cap;

  if (!(sp->s_feature_incompat & MFS_INCOMPAT_JOURNAL) || sp->s_rd_only)
	return(OK);

  jr.dev = dev;
  jr.jstart = sp->s_journal_start;
  jr.jblocks = sp->s_journal_blocks;
  jr.bsize = sp->s_block_size;

  /* The accumulation buffer holds the home block numbers of the current
   * transaction.  It is sized well above one journal's worth so that a large
   * transaction is chunked across commits (back-pressure) rather than hitting
   * the unjournalled fallback, and to give later batching headroom. */
  jr.txn_max = 8192;
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
  jr.batch_ops = 0;

  /* Batching thresholds.  Commit the running transaction once it has gathered
   * roughly a quarter of the journal's atomic capacity in metadata blocks, or
   * after a bounded number of requests -- whichever comes first.
   *
   * Two ceilings matter.  Staying well under the journal capacity keeps each
   * committed batch a single (atomic) chunk.  Staying well under the block
   * cache (DEFAULT_NR_BUFS) keeps the batch's uncommitted, still-dirty metadata
   * resident in the cache as recently-used buffers, so the cache never has to
   * evict one to its home location ahead of the commit -- which would break
   * write-ahead ordering.  The request bound also limits how much recently
   * written (un-synced) work a crash can roll back. */
  cap = journal_capacity();
  jr.commit_blocks = cap / 4;
  if (jr.commit_blocks > DEFAULT_NR_BUFS / 4) jr.commit_blocks = DEFAULT_NR_BUFS / 4;
  if (jr.commit_blocks < 1) jr.commit_blocks = 1;
  jr.commit_ops = cap / 4;
  if (jr.commit_ops < 8) jr.commit_ops = 8;
  if (jr.commit_ops > 64) jr.commit_ops = 64;

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
/* Commit the current transaction.  For each capacity-sized chunk of dirtied
 * metadata: write it to the journal (a multi-descriptor transaction, durable
 * once its commit block lands), checkpoint those blocks to their home
 * locations, then advance the journal.  A transaction that fits the journal --
 * the normal case -- is a single chunk and therefore atomic. */
  struct inode *rip;
  unsigned cap, lo, hi;
  u32_t seq;
  int r;

  if (!jr.active) return(OK);

  jr.batch_ops = 0;		/* a new batch starts after this commit */

  /* Flush dirty in-core inodes into their cache blocks; this MARKDIRTYs (and
   * so journal_track()s) the inode blocks. */
  for (rip = &inode[0]; rip < &inode[NR_INODES]; rip++)
	if (rip->i_count > 0 && IN_ISDIRTY(rip)) rw_inode(rip, WRITING);

  /* data=ordered: force this transaction's file data to its home location
   * before the metadata is committed, so committed metadata never references
   * data that a crash left unwritten. */
  flush_data();

  if (jr.txn_count == 0) return(OK);

  /* The block-number buffer overflowed (a single request dirtied more metadata
   * than the buffer holds); we no longer have the full block list, so fall back
   * to an unjournalled flush.  This needs a pathologically large operation and
   * a tiny journal, and is the only path that is not crash-atomic. */
  if (jr.txn_count > jr.txn_max) {
	printf("MFS: journal transaction too large (%u blocks); "
		"flushing unjournalled\n", jr.txn_count);
	lmfs_flushall();
	jr.txn_count = 0;
	jr.crash_skip = 0;
	return(OK);
  }

  cap = journal_capacity();

  for (lo = 0; lo < jr.txn_count; lo = hi) {
	hi = lo + cap;
	if (hi > jr.txn_count) hi = jr.txn_count;
	seq = jr.sequence;

	/* Write this chunk to the journal and make it durable. */
	if ((r = write_txn(lo, hi, seq)) != OK) return(r);

	if (jr.crash_skip || jr.crashed) {
		/* Test hook: simulate a crash right after the commit -- leave the
		 * transaction un-checkpointed and the journal un-advanced.  The
		 * crash test always uses a single-chunk (atomic) transaction. */
		jr.sequence = seq + 1;
		jr.txn_count = 0;
		jr.crash_skip = 0;
		return(OK);
	}

	/* Checkpoint this chunk in place.  The last (or only) chunk uses the
	 * cache's bulk flush; earlier chunks of an over-capacity transaction
	 * write their blocks directly so the bulk flush only handles the rest. */
	if (hi == jr.txn_count)
		lmfs_flushall();
	else if ((r = checkpoint_blocks(lo, hi)) != OK)
		return(r);

	/* Advance: every transaction up to 'seq' is now checkpointed. */
	if ((r = journal_advance(seq + 1)) != OK) return(r);
	jr.sequence = seq + 1;
  }

  jr.txn_count = 0;
  jr.crash_skip = 0;
  return(OK);
}

/*===========================================================================*
 *				journal_maybe_commit			     *
 *===========================================================================*/
int journal_maybe_commit(void)
{
/* Called after every request.  Rather than commit each request on its own,
 * accumulate a running transaction and commit it only once it has gathered
 * enough metadata or spanned enough requests -- batching that amortises the
 * journal write and the in-place checkpoint over many operations (the JBD
 * model).  Durability still arrives promptly: fsync()/sync() force a commit (via
 * fs_sync), unmount commits, and the request bound caps how much a crash rolls
 * back.  Because each batch is checkpointed and the journal advanced before the
 * next begins, a freed metadata block can never be replayed over its later
 * reuse, so no revoke records are needed. */
  if (!jr.active) return(OK);

  /* The crash-test hook forces an immediate commit so the armed transaction is
   * actually written to the journal. */
  if (jr.crash_skip || jr.crashed) return(journal_commit());

  jr.batch_ops++;
  if (jr.txn_count >= jr.commit_blocks || jr.batch_ops >= jr.commit_ops)
	return(journal_commit());

  return(OK);
}
