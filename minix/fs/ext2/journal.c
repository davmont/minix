/* ext3 (JBD) journal recovery for the ext2 server.
 *
 * When an ext3 file system is mounted with the INCOMPAT_RECOVER feature set,
 * its journal holds committed transactions that were not yet checkpointed to
 * their home locations before a crash.  Rather than mounting read-only and
 * ignoring the journal, replay it here so the file system can be mounted
 * read-write in a consistent state.
 *
 * Only the classic JBD format is handled (32-bit block tags, no checksums and
 * no 64-bit block numbers -- i.e. an ext3 journal created without the journal
 * checksum / 64bit features), which is what mke2fs -t ext3 produces.  All
 * journal metadata is big-endian.
 */
#include "fs.h"
#include "buf.h"
#include "inode.h"
#include "super.h"
#include <stdlib.h>
#include <string.h>

#define EXT2_JOURNAL_INO	8	/* standard internal journal inode */

#define JFS_MAGIC		0xc03b3998U
#define JFS_DESCRIPTOR_BLOCK	1
#define JFS_COMMIT_BLOCK	2
#define JFS_SUPERBLOCK_V1	3
#define JFS_SUPERBLOCK_V2	4
#define JFS_REVOKE_BLOCK	5

#define JFS_FLAG_ESCAPE		1	/* on-disk block was escaped */
#define JFS_FLAG_SAME_UUID	2	/* block has same uuid as previous */
#define JFS_FLAG_DELETED	4	/* block deleted by this transaction */
#define JFS_FLAG_LAST_TAG	8	/* last tag in this descriptor block */

#define JPASS_SCAN	0
#define JPASS_REVOKE	1
#define JPASS_REPLAY	2

/* Big-endian accessors for journal metadata. */
static u32_t jbe32(const void *p)
{
	const u8_t *b = (const u8_t *) p;

	return ((u32_t) b[0] << 24) | ((u32_t) b[1] << 16) |
	    ((u32_t) b[2] << 8) | (u32_t) b[3];
}

static void jbe32_put(void *p, u32_t v)
{
	u8_t *b = (u8_t *) p;

	b[0] = (u8_t)(v >> 24);
	b[1] = (u8_t)(v >> 16);
	b[2] = (u8_t)(v >> 8);
	b[3] = (u8_t) v;
}

/* JBD sequence numbers wrap; compare modulo 2^32. */
static int seq_after(u32_t a, u32_t b)
{
	return (i32_t)(a - b) > 0;
}

/* Revoke table: a block revoked at sequence S must not be replayed from any
 * transaction with sequence <= S. */
struct jrevoke { u32_t block; u32_t seq; };
static struct jrevoke *revtab;
static unsigned revcnt, revmax;

static int rev_add(u32_t block, u32_t seq)
{
	unsigned i;

	for (i = 0; i < revcnt; i++)
		if (revtab[i].block == block) {
			if (seq_after(seq, revtab[i].seq))
				revtab[i].seq = seq;
			return OK;
		}
	if (revcnt >= revmax)
		return ENOMEM;
	revtab[revcnt].block = block;
	revtab[revcnt].seq = seq;
	revcnt++;
	return OK;
}

static int rev_revoked(u32_t block, u32_t seq)
{
/* TRUE if 'block' is revoked at a sequence >= seq (so skip its replay). */
	unsigned i;

	for (i = 0; i < revcnt; i++)
		if (revtab[i].block == block && !seq_after(seq, revtab[i].seq))
			return TRUE;
	return FALSE;
}

/* Read journal logical block 'lb' (through the journal inode's block map). */
static struct buf *jread(struct inode *jp, u32_t lb)
{
	return get_block_map(jp, (u64_t) lb * jp->i_sp->s_block_size);
}

/*===========================================================================*
 *				do_one_pass				     *
 *===========================================================================*/
static int do_one_pass(struct super_block *sp, struct inode *jp, u32_t maxlen,
	u32_t first, u32_t start, u32_t start_seq, int pass, u32_t end_seq,
	u32_t *final_seq)
{
/* Walk the log once from block 'start' / sequence 'start_seq'.  SCAN finds the
 * end of the committed log (returned in *final_seq); REVOKE collects revoke
 * records; REPLAY writes journaled blocks to their home locations. */
	unsigned bs = sp->s_block_size;
	u32_t next = start, seq = start_seq;
	unsigned guard = 0, guard_max = maxlen + 2;

	for (;;) {
		struct buf *bp;
		u32_t magic, btype, bseq;

		/* REVOKE/REPLAY only process committed transactions. */
		if (pass != JPASS_SCAN && seq == end_seq)
			break;
		if (++guard > guard_max)
			break;			/* malformed log; stop */

		if ((bp = jread(jp, next)) == NULL)
			break;
		magic = jbe32((u8_t *) b_data(bp) + 0);
		btype = jbe32((u8_t *) b_data(bp) + 4);
		bseq  = jbe32((u8_t *) b_data(bp) + 8);

		if (magic != JFS_MAGIC || bseq != seq) {
			put_block(bp);
			break;			/* end of the valid log */
		}

		if (btype == JFS_DESCRIPTOR_BLOCK) {
			unsigned off = 12;
			u32_t dblock = next;
			int last = 0;

			while (!last && off + 8 <= bs) {
				u32_t tblock = jbe32((u8_t *) b_data(bp) + off);
				u32_t tflags = jbe32((u8_t *) b_data(bp) +
				    off + 4);

				off += 8;
				if (!(tflags & JFS_FLAG_SAME_UUID))
					off += 16;
				last = (tflags & JFS_FLAG_LAST_TAG) ? 1 : 0;

				/* The data block for this tag is the next log
				 * block. */
				if (++dblock >= maxlen) dblock = first;

				if (pass == JPASS_REPLAY &&
				    !(tflags & JFS_FLAG_DELETED) &&
				    !rev_revoked(tblock, seq)) {
					struct buf *db, *hb;

					if ((db = jread(jp, dblock)) == NULL)
						continue;
					hb = get_block(sp->s_dev, (block64_t) tblock,
					    NO_READ);
					if (hb != NULL) {
						memcpy(b_data(hb), b_data(db), bs);
						if (tflags & JFS_FLAG_ESCAPE)
							jbe32_put(b_data(hb),
							    JFS_MAGIC);
						lmfs_markdirty(hb);
						put_block(hb);
					}
					put_block(db);
				}
			}
			put_block(bp);
			next = dblock;
			if (++next >= maxlen) next = first;
		} else if (btype == JFS_COMMIT_BLOCK) {
			put_block(bp);
			seq++;			/* transaction is durable */
			if (++next >= maxlen) next = first;
		} else if (btype == JFS_REVOKE_BLOCK) {
			if (pass == JPASS_REVOKE) {
				u32_t rcount = jbe32((u8_t *) b_data(bp) + 12);
				unsigned o = 16;

				if (rcount > bs) rcount = bs;
				while (o + 4 <= rcount) {
					if (rev_add(jbe32((u8_t *) b_data(bp) +
					    o), seq) != OK)
						break;
					o += 4;
				}
			}
			put_block(bp);
			if (++next >= maxlen) next = first;
		} else {
			put_block(bp);
			break;			/* unknown block type: stop */
		}
	}

	if (final_seq != NULL)
		*final_seq = seq;
	return OK;
}

/*===========================================================================*
 *				ext2_journal_recover			     *
 *===========================================================================*/
int ext2_journal_recover(struct super_block *sp)
{
/* Replay the JBD journal of *sp.  Returns OK on success (the file system is now
 * consistent and the journal is reset), or an error if recovery could not be
 * performed (the caller then falls back to a read-only mount). */
	struct inode *jp;
	struct buf *bp;
	u32_t magic, btype, maxlen, first, jstart, jseq, jblocksize;
	u32_t end_seq = 0;
	int r = OK;

	if ((jp = get_inode(sp->s_dev, EXT2_JOURNAL_INO)) == NULL)
		return EINVAL;

	/* Journal superblock is journal logical block 0. */
	if ((bp = jread(jp, 0)) == NULL) {
		put_inode(jp);
		return EIO;
	}
	magic      = jbe32((u8_t *) b_data(bp) + 0);
	btype      = jbe32((u8_t *) b_data(bp) + 4);
	jblocksize = jbe32((u8_t *) b_data(bp) + 12);
	maxlen     = jbe32((u8_t *) b_data(bp) + 16);
	first      = jbe32((u8_t *) b_data(bp) + 20);
	jseq       = jbe32((u8_t *) b_data(bp) + 24);
	jstart     = jbe32((u8_t *) b_data(bp) + 28);
	put_block(bp);

	if (magic != JFS_MAGIC ||
	    (btype != JFS_SUPERBLOCK_V1 && btype != JFS_SUPERBLOCK_V2) ||
	    jblocksize != sp->s_block_size || maxlen < 1 || first < 1 ||
	    first >= maxlen) {
		put_inode(jp);
		return EINVAL;		/* not a journal we understand */
	}

	if (jstart == 0) {
		/* The journal is empty: nothing to replay. */
		put_inode(jp);
		return OK;
	}

	/* Set up the revoke table (bounded; fall back to RO if it overflows). */
	revmax = 4096;
	revcnt = 0;
	if ((revtab = malloc(revmax * sizeof(*revtab))) == NULL) {
		put_inode(jp);
		return ENOMEM;
	}

	/* Pass 1: find the end of the committed log. */
	do_one_pass(sp, jp, maxlen, first, jstart, jseq, JPASS_SCAN, 0,
	    &end_seq);

	if (end_seq != jseq) {
		/* Pass 2: collect revoke records up to the end. */
		do_one_pass(sp, jp, maxlen, first, jstart, jseq, JPASS_REVOKE,
		    end_seq, NULL);
		/* Pass 3: replay journaled blocks to their home locations. */
		do_one_pass(sp, jp, maxlen, first, jstart, jseq, JPASS_REPLAY,
		    end_seq, NULL);
		lmfs_flushall();
	}

	free(revtab);
	revtab = NULL;

	/* Reset the journal superblock: empty (start 0), next sequence. */
	if ((bp = jread(jp, 0)) != NULL) {
		jbe32_put((u8_t *) b_data(bp) + 24, end_seq);	/* s_sequence */
		jbe32_put((u8_t *) b_data(bp) + 28, 0);		/* s_start */
		lmfs_markdirty(bp);
		put_block(bp);
		lmfs_flushall();
	}

	put_inode(jp);
	return r;
}
