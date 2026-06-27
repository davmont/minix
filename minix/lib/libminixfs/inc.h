#ifndef _LIBMINIXFS_INC_H
#define _LIBMINIXFS_INC_H

int lmfs_get_partial_block(struct buf **bpp, dev_t dev, block64_t block,
	int how, size_t block_size);
void lmfs_readahead(dev_t dev, block64_t base_block, unsigned int nblocks,
	size_t last_size);
unsigned int lmfs_readahead_limit(void);

/* Install (or clear, with NULL) the disk-transfer hook used by the block cache;
 * implemented in cache.c, used by the multithreaded glue in cache_mt.c. */
void lmfs_set_io_hook(ssize_t (*hook)(int, dev_t, u64_t, char *, iovec_t *,
	int, size_t));

#endif /* !_LIBMINIXFS_INC_H */
