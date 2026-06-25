/* Block-cache helpers for the exFAT server. */
#include "fs.h"

/*===========================================================================*
 *				get_block				     *
 *===========================================================================*/
struct buf *get_block(dev_t dev, block64_t block, int how)
{
/* Wrapper around lmfs_get_block().  Like the vfat/ext2 servers, we cannot
 * recover from block read errors in most code paths, so panic on I/O failure.
 */
	struct buf *bp;
	int r;

	if ((r = lmfs_get_block(&bp, dev, block, how)) != OK && r != ENOENT)
		panic("exfat: error getting block (%llu,%llu): %d",
		    (unsigned long long)dev, (unsigned long long)block, r);

	assert(r == OK || how == PEEK);

	return (r == OK) ? bp : NULL;
}

/*===========================================================================*
 *				put_block				     *
 *===========================================================================*/
void put_block(struct buf *bp)
{
	if (bp == NULL)
		return;

	lmfs_put_block(bp);
}
