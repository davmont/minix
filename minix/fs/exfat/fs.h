#ifndef EXFAT_FS_H
#define EXFAT_FS_H

/* Master header for the exFAT file server. */
#define _SYSTEM		1	/* tell headers this is a system service */

/* The following are so basic, all the *.c files get them automatically. */
#include <minix/config.h>	/* MUST be first */
#include <sys/types.h>
#include <minix/const.h>
#include <minix/type.h>

#include <lib.h>
#include <limits.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include <minix/syslib.h>
#include <minix/sysutil.h>
#include <minix/libminixfs.h>
#include <minix/bdev.h>
#include <minix/fsdriver.h>

#include "exfat.h"

#include "const.h"
#include "type.h"
#include "proto.h"
#include "glo.h"

#endif /* EXFAT_FS_H */
