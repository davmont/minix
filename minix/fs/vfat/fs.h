#ifndef VFAT_FS_H
#define VFAT_FS_H

/* Master header for the vfat (FAT/MS-DOS) file server. */
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

#include "bpb.h"
#include "bootsect.h"
#include "direntry.h"
#include "fat.h"

#include "const.h"
#include "type.h"
#include "proto.h"
#include "glo.h"

#endif /* VFAT_FS_H */
