# MFS metadata journaling — design

Status: **Phase 2a (data=ordered) complete.** Phase 0 (substrate), Phase 1 (the
write-ahead log engine + recovery), and Phase 2a (data=ordered) are implemented
and validated; Phase 2b (multi-descriptor + back-pressure) is next, then Phase 2c
(batching).
Feature bit: `MFS_INCOMPAT_JOURNAL` (0x0002), a V4 incompat feature.

## 1. Goal

After an unclean shutdown, MFS today refuses a read/write mount and requires an
offline `fsck` (the clean/dirty superblock flag, `mount.c`).  Journaling makes
recovery automatic and fast: the filesystem is brought to a consistent state at
mount time by replaying a log, with no full scan.

Scope: **metadata journaling** (inodes, the inode/zone bitmaps, directory
blocks, indirect blocks, and the superblock).  File *data* is not journalled:
under **data=ordered** (ext3's default) a file's data blocks are forced to their
home locations *before* the metadata transaction that references them commits, so
committed metadata never points at data a crash left unwritten.  A crash may lose
the very last unsynced writes, but the filesystem structure — and the data any
committed metadata points to — is always consistent.  `MARKDIRTY` (metadata) and
`MARKDIRTY_DATA` (regular-file data) split the two classes at the point a block
is dirtied; `journal_track_data` collects the data blocks and `flush_data()`
writes them out ahead of each commit.

## 2. Crash-consistency model

A **physical redo log** with **write-ahead logging (WAL)**:

1. A *transaction* is the set of metadata blocks dirtied by one filesystem
   operation (create, unlink, rename, mkdir, truncate, a write that allocates,
   chmod/chown/utime, ...).
2. On commit, the new contents of those blocks are written **to the journal**
   first, followed by a commit record; only **after** the journal is durably on
   disk are the blocks written **in place** (checkpointed).
3. On recovery, every committed-but-not-checkpointed transaction in the journal
   is replayed (its blocks are written to their home locations); an incomplete
   (uncommitted) transaction at the tail is discarded.

Because the home locations are only ever written *after* the journal copy is
durable, a crash at any point leaves either the old consistent state (journal
not committed) or a state the journal can complete (journal committed) — never
a torn in-place update with no redo record.

### WAL ordering

`lmfs_flushall()` flushes *all* dirty cache blocks with no ordering between
them, so it cannot provide the journal-before-home barrier.  The journal is
therefore written with **direct, synchronous device I/O** (`bdev_*`, already
used by `mount.c`), which the home-location writes (via the lmfs cache) never
race:

```
commit(txn):
  for each block b in txn: read current contents from the cache
  bdev_write: descriptor block (home block numbers) + the data blocks
  bdev_write: commit block (sequence number + checksum)   <-- journal durable
  lmfs_flushall()                                          <-- home writes
  bdev_write: advance journal tail past this txn           <-- txn reclaimed
```

## 3. On-disk format

`MFS_INCOMPAT_JOURNAL` set in `s_feature_incompat`.  The journal is a
contiguous run of blocks reserved by `mkfs`; its location lives in two of the
V4 superblock reserved words:

```
s_v4_reserved[0]  ->  s_journal_start   (first journal block)
s_v4_reserved[1]  ->  s_journal_blocks  (journal length in blocks)
```

The journal is a circular log.  Block 0 of the journal is the **journal
superblock**:

```
jsb_magic       u32   "MJL1"
jsb_blocks      u32   total journal blocks (incl. this one)
jsb_sequence    u32   sequence number of the oldest valid transaction
jsb_start       u32   block offset (within the journal) of the oldest txn
jsb_flags       u32   (reserved)
```

Each transaction is a run of blocks:

```
[ descriptor ]  jd_magic="MJD1", jd_sequence, jd_count, jd_target[jd_count]
[ data blk 1 ]  new contents of home block jd_target[0]
   ...
[ data blk n ]  new contents of home block jd_target[n-1]
[ commit     ]  jc_magic="MJC1", jc_sequence, jc_checksum
```

`jd_count` is bounded so a descriptor's target list fits one block
(block_size/4 - header).  Larger transactions use multiple descriptor groups.

## 4. Recovery (mount time)

When mounting a journalled FS that was not cleanly unmounted:

1. Read the journal superblock.
2. Starting at `jsb_start`/`jsb_sequence`, scan transactions: for each, read its
   descriptor and verify the matching commit record (sequence + checksum).
3. For every transaction with a valid commit, write its data blocks to their
   home `jd_target` locations (replay).  Stop at the first transaction with no
   valid commit (the torn tail).
4. Flush, then reset the journal (empty) and mark the FS clean.

Replay is **idempotent** (it just rewrites home blocks), so a crash during
recovery is safe — recovery simply runs again.

## 5. Transaction tracking

`MARKDIRTY(bp)` (clean.h) and `IN_MARKDIRTY`/`rw_inode` are the points where MFS
dirties a metadata block.  `MARKDIRTY` is extended to also record the block in
the current transaction (`journal_track(bp->lmfs_blocknr)`).  Because the MFS
server processes one request at a time and commits at the end of each modifying
request, the set of dirty metadata blocks at commit time is exactly that
request's transaction.

The commit point is a new `fdr_postcall` hook (libfsdriver already calls it
after every request): if the journal has tracked blocks, commit the
transaction.  `fs_sync` and unmount also commit.

## 6. Phased plan

* **Phase 0 — substrate (this change).** On-disk journal area created by
  `mkfs.mfs -j` (sets `MFS_INCOMPAT_JOURNAL`, reserves the blocks, writes the
  journal superblock); the MFS driver recognises the feature, reads the journal
  location, and at mount time runs recovery (a no-op on a clean journal).
  `fsck.mfs` understands the feature.  Validates the format and plumbing
  end to end without yet changing write behaviour.
* **Phase 1 — the log engine (this change).** `journal.c`: `journal_track`,
  `journal_commit` (descriptor + data + commit via `bdev`, then checkpoint, then
  advance), and `journal_recover` (replay).  `MARKDIRTY` is hooked to track
  dirtied blocks, and `fdr_postcall` commits one transaction per request.
  Validated three ways: heavy metadata churn stays `fsck`-clean; a clean remount
  runs recovery and finds nothing pending; and a fault-injection build
  (`-DJOURNAL_CRASH_TEST`) commits a transaction to the journal, latches off all
  checkpointing so the home blocks stay stale, and is then hard-killed — the next
  mount replays the journal and the change reappears, `fsck`-clean.
* **Phase 2a — data=ordered (this change).** Split metadata from regular-file
  data at the `MARKDIRTY`/`MARKDIRTY_DATA` hooks; journal only metadata, and
  force a transaction's file data to its home location before the metadata
  commit.  Keeps transactions small (a large write no longer overflows the
  journal) and halves journal write traffic, while preserving the consistency
  guarantee.  Validated: heavy churn fsck-clean; a multi-megabyte file
  round-trips with identical checksum across remount; crash recovery still
  replays metadata and committed data survives, fsck-clean.
* **Phase 2b — multi-descriptor transactions + journal-full back-pressure.**
  Let one transaction span several descriptor blocks (so a large batched
  transaction fits), and force a checkpoint when the journal would overflow.
* **Phase 2c — batching.** Accumulate a running transaction across requests and
  commit on sync / size threshold / unmount, amortising the per-request journal
  write and checkpoint (the JBD performance model).

## 7. Risks / notes

* Journaling bugs cause silent corruption, so every phase is validated against
  `fsck.mfs` and replay is kept idempotent and verify-checked.
* The journal uses `bdev` directly for ordering; the home writes stay in the
  lmfs cache.  The two never target the same blocks within a commit window.
* `mkfs` reserves the journal out of the data zones; the journal blocks are
  marked allocated in the zone bitmap so the allocator never hands them out.
