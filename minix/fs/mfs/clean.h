
#ifndef _MFS_CLEAN_H
#define _MFS_CLEAN_H 1

/* MARKDIRTY marks a *metadata* block dirty: under a journalled mount its new
 * contents are written to the journal (journal_track) and only checkpointed in
 * place after the transaction commits.  MARKDIRTY_DATA marks a regular-file
 * *data* block dirty: data is never journalled; instead, under data=ordered, it
 * is forced to its home location before the metadata transaction commits
 * (journal_track_data), so committed metadata never points at unwritten data. */
#define MARKDIRTY(b) do { \
	if (superblock.s_rd_only) { \
		printf("%s:%d: dirty block on rofs! ", __FILE__, __LINE__); \
		util_stacktrace(); \
	} else { \
		lmfs_markdirty(b); \
		journal_track((u32_t) (b)->lmfs_blocknr); \
	} \
} while(0)

#define MARKDIRTY_DATA(b) do { \
	if (superblock.s_rd_only) { \
		printf("%s:%d: dirty block on rofs! ", __FILE__, __LINE__); \
		util_stacktrace(); \
	} else { \
		lmfs_markdirty(b); \
		journal_track_data((u32_t) (b)->lmfs_blocknr); \
	} \
} while(0)

/* Block kind passed to zero_block(): a freshly zeroed block is either metadata
 * (a new indirect block) or regular-file data (a new file block). */
#define ZB_META	0
#define ZB_DATA	1

#endif
