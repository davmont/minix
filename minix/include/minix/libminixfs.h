/* Prototypes for -lminixfs. */

#ifndef _MINIX_FSLIB_H
#define _MINIX_FSLIB_H

#include <minix/fsdriver.h>

/* Maximum number of blocks that will be considered by lmfs_prefetch() */
#define LMFS_MAX_PREFETCH	NR_IOREQS

struct buf {
  /* Data portion of the buffer. */
  void *data;

  /* Header portion of the buffer - internal to libminixfs. */
  struct buf *lmfs_next;       /* used to link all free bufs in a chain */
  struct buf *lmfs_prev;       /* used to link all free bufs the other way */
  struct buf *lmfs_hash;       /* used to link bufs on hash chains */
  dev_t lmfs_dev;              /* major | minor device where block resides */
  block64_t lmfs_blocknr;      /* block number of its (minor) device */
  char lmfs_count;             /* number of users of this buffer */
  char lmfs_needsetcache;      /* to be identified to VM */
  size_t lmfs_bytes;           /* size of this block (allocated and used) */
  u32_t lmfs_flags;            /* Flags shared between VM and FS */

  /* If any, which inode & offset does this block correspond to?
   * If none, VMC_NO_INODE
   */
  ino_t lmfs_inode;
  u64_t lmfs_inode_offset;
};

void lmfs_markdirty(struct buf *bp);
void lmfs_markclean(struct buf *bp);
int lmfs_isclean(struct buf *bp);
void lmfs_flushall(void);
void lmfs_flushdev(dev_t dev);
size_t lmfs_fs_block_size(void);
void lmfs_may_use_vmcache(int);
int lmfs_vmcache_enabled(void);
void lmfs_set_blocksize(size_t blocksize);
void lmfs_buf_pool(int new_nr_bufs);
int lmfs_get_block(struct buf **bpp, dev_t dev, block64_t block, int how);
int lmfs_get_block_ino(struct buf **bpp, dev_t dev, block64_t block, int how,
	ino_t ino, u64_t off);
void lmfs_put_block(struct buf *bp);
void lmfs_free_block(dev_t dev, block64_t block);
void lmfs_zero_block_ino(dev_t dev, ino_t ino, u64_t off);
void lmfs_invalidate(dev_t device);
void lmfs_prefetch(dev_t dev, const block64_t *blockset, unsigned int nblocks);
void lmfs_setquiet(int q);
void lmfs_set_blockusage(fsblkcnt_t btotal, fsblkcnt_t bused);
void lmfs_change_blockusage(int delta);

/* get_block arguments */
#define NORMAL             0    /* forces get_block to do disk read */
#define NO_READ            1    /* prevents get_block from doing disk read */
#define PEEK               2    /* returns ENOENT if not in cache */

/* Block I/O helper functions. */
void lmfs_driver(dev_t dev, char *label);
ssize_t lmfs_bio(dev_t dev, struct fsdriver_data *data, size_t bytes,
	off_t pos, int call);
void lmfs_bflush(dev_t dev);

/* Multithreading support (see cache_mt.c).  A file system that runs multiple
 * worker threads calls lmfs_enable_mt() once at startup so that the block cache
 * performs its disk transfers asynchronously, yielding the calling worker while
 * the disk driver is busy.  The file system's main thread must then pass every
 * reply it receives from the disk driver (BDEV_REPLY messages) to
 * lmfs_mt_reply(), which routes the reply to the worker waiting for it. */
void lmfs_enable_mt(void);
void lmfs_mt_reply(message *m);

#endif /* _MINIX_FSLIB_H */
