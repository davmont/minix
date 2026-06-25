#include "fs.h"
#include "inode.h"
#include "clean.h"

/*===========================================================================*
 *				fs_sync					     *
 *===========================================================================*/
void fs_sync(void)
{
/* Perform the sync() system call.  Flush all the tables. 
 * The order in which the various tables are flushed is critical.  The
 * blocks must be flushed last, since rw_inode() leaves its results in
 * the block cache.
 */
  struct inode *rip;

  /* Write all the dirty inodes to the disk. */
  for(rip = &inode[0]; rip < &inode[NR_INODES]; rip++)
	  if(rip->i_count > 0 && IN_ISDIRTY(rip)) rw_inode(rip, WRITING);

  /* If journaling, commit the dirty metadata as a transaction (journal first,
   * then write it to its home locations); journal_commit() is a no-op on a
   * non-journalled or read-only mount. */
  journal_commit();

  /* Write all the (remaining) dirty blocks to the disk. */
#ifdef JOURNAL_CRASH_TEST
  /* After a simulated crash, suppress the checkpoint so the home blocks stay
   * stale and journal replay is the only way the change can survive. */
  if (journal_crashed()) return;
#endif
  lmfs_flushall();
}

/*===========================================================================*
 *				fs_postcall				     *
 *===========================================================================*/
void fs_postcall(void)
{
/* Called by libfsdriver after every request.  Commit the metadata that the
 * request dirtied as one journal transaction, so each operation is atomic with
 * respect to a crash. */
  journal_commit();
}
