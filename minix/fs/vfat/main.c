/* The vfat (FAT12/16/32, read-only) file server for MINIX.
 *
 * Built on libfsdriver + libminixfs, modeled on the ext2 server.  The on-disk
 * format logic is derived from NetBSD's sys/fs/msdosfs.
 */
#include "fs.h"
#include <minix/optset.h>

static void sef_local_startup(void);
static int sef_cb_init_fresh(int type, sef_init_info_t *info);
static void sef_cb_signal_handler(int signo);

EXTERN int env_argc;
EXTERN char **env_argv;

int main(int argc, char *argv[])
{
	env_setargs(argc, argv);
	sef_local_startup();

	/* The fsdriver library runs the request loop. */
	fsdriver_task(&vfat_table);

	return 0;
}

static void sef_local_startup(void)
{
	sef_setcb_init_fresh(sef_cb_init_fresh);
	sef_setcb_signal_handler(sef_cb_signal_handler);
	sef_startup();
}

static int sef_cb_init_fresh(int UNUSED(type), sef_init_info_t *UNUSED(info))
{
	/* Initialize the file server. */
	mounted = FALSE;
	pmp = NULL;

	init_inode_cache();

	/* Small buffer pool until the real block size is known at mount time. */
	lmfs_buf_pool(10);

	return OK;
}

static void sef_cb_signal_handler(int signo)
{
	/* Only check for termination signal, ignore anything else. */
	if (signo != SIGTERM) return;

	fsdriver_terminate();
}
