#include "fs.h"
#include "buf.h"
#include "inode.h"

/* Number of worker threads used to serve requests concurrently.  Chosen to be
 * at least VFS's worker-thread count, so that VFS can never have more requests
 * outstanding to this file system than there are workers to handle them. */
#define MFS_NR_WORKERS	12

/* SEF functions and variables. */
static void sef_local_startup(void);
static int sef_cb_init_fresh(int type, sef_init_info_t *info);
static void sef_cb_signal_handler(int signo);

/*===========================================================================*
 *				main                                         *
 *===========================================================================*/
int main(int argc, char *argv[])
{
/* This is the main routine of this service. */

  /* SEF local startup. */
  env_setargs(argc, argv);
  sef_local_startup();

  /* The fsdriver library does the actual work here.  Run multithreaded: the
   * block cache performs its disk I/O asynchronously (lmfs_enable_mt(), done in
   * sef_cb_init_fresh) so that a worker blocked on the disk does not stall the
   * others, and replies from the disk driver are routed to the waiting workers
   * by mfs_other() below.
   */
  fsdriver_mt_task(&mfs_table, MFS_NR_WORKERS);

  return(0);
}

/*===========================================================================*
 *				mfs_other				     *
 *===========================================================================*/
void mfs_other(const message *m_ptr, int ipc_status)
{
/* Handle a message that is not a file-system request from VFS.  When running
 * multithreaded, the asynchronous block cache receives its disk-driver replies
 * here (on the main thread); hand them to the cache so it can wake the worker
 * thread waiting for each one.  Anything else is ignored.
 */
  message m;

  if (is_ipc_notify(ipc_status))
	return;

  if (m_ptr->m_type == BDEV_REPLY) {
	m = *m_ptr;		/* lmfs_mt_reply()/libbdev want a writable copy */
	lmfs_mt_reply(&m);
  }
}

/*===========================================================================*
 *			       sef_local_startup			     *
 *===========================================================================*/
static void sef_local_startup()
{
  /* Register init callbacks. */
  sef_setcb_init_fresh(sef_cb_init_fresh);
  sef_setcb_init_restart(SEF_CB_INIT_RESTART_STATEFUL);

  /* Register signal callbacks. */
  sef_setcb_signal_handler(sef_cb_signal_handler);

  /* Let SEF perform startup. */
  sef_startup();
}

/*===========================================================================*
 *		            sef_cb_init_fresh                                *
 *===========================================================================*/
static int sef_cb_init_fresh(int UNUSED(type), sef_init_info_t *UNUSED(info))
{
/* Initialize the Minix file server. */
  int i;

  lmfs_may_use_vmcache(1);

  /* Init inode table */
  for (i = 0; i < NR_INODES; ++i) {
	inode[i].i_count = 0;
	cch[i] = 0;
  }
	
  init_inode_cache();

  lmfs_buf_pool(DEFAULT_NR_BUFS);

  /* Run the block cache in multithreaded mode: disk transfers become
   * asynchronous so that one worker's I/O does not block the others. */
  lmfs_enable_mt();

  return(OK);
}

/*===========================================================================*
 *		           sef_cb_signal_handler                             *
 *===========================================================================*/
static void sef_cb_signal_handler(int signo)
{
  /* Only check for termination signal, ignore anything else. */
  if (signo != SIGTERM) return;

  fs_sync();

  fsdriver_terminate();
}


#if 0
/*===========================================================================*
 *				cch_check				     *	
 *===========================================================================*/
static void cch_check(void) 
{
  int i;

  for (i = 0; i < NR_INODES; ++i) {
	if (inode[i].i_count != cch[i] && req_nr != REQ_GETNODE &&
	    req_nr != REQ_PUTNODE && req_nr != REQ_READSUPER &&
	    req_nr != REQ_MOUNTPOINT && req_nr != REQ_UNMOUNT &&
	    req_nr != REQ_SYNC && req_nr != REQ_LOOKUP) {
		printf("MFS(%d) inode(%lu) cc: %d req_nr: %d\n", sef_self(),
			inode[i].i_num, inode[i].i_count - cch[i], req_nr);
	}
	  
	cch[i] = inode[i].i_count;
  }
}
#endif

