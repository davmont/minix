#define _EXPOSE_MMC
#include <sys/cdefs.h>

/*
 * Copyright (c) 2006, 2008, 2013, 2021, 2022 Reinoud Zandijk
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <assert.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <util.h>

#include <sys/cdio.h>
#include <sys/disklabel.h>
#include <sys/dkio.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <fs/udf/udf_mount.h>

#include "mountprog.h"
#include "newfs_udf.h"
#include "udf_create.h"
#include "udf_write.h"

/* global variables describing disc and format requests */
struct mmc_discinfo mmc_discinfo;
int	 format_flags = 0;
int	 media_accesstype = 0;
int	 check_surface = 0;
int	 wrtrack_skew = 0;
float	 meta_fract = 0.0;

char *format_str = NULL;
int   dev_fd = -1;
char *dev_name = NULL;

int emul_mmc_profile = -1;
int emul_packetsize = 1;
int emul_sectorsize = 512;
off_t emul_size = 0;

/* --------------------------------------------------------------------- */

int
udf_write_sector(void *sector, uint64_t location)
{
	uint64_t wpos;
	ssize_t ret;

	if (dev_fd == -1)
		return EBADF;

	wpos = (uint64_t) location * context.sector_size;
	ret = pwrite(dev_fd, sector, context.sector_size, wpos);
	if (ret == -1)
		return errno;
	if (ret < (int) context.sector_size)
		return EIO;
	return 0;
}

int
udf_surface_check(void)
{
	return 0;
}

int
udf_update_trackinfo(struct mmc_discinfo *di, struct mmc_trackinfo *ti)
{
	int error;

	if (dev_fd != -1) {
		error = ioctl(dev_fd, MMCGETTRACKINFO, ti);
		if (error == 0)
			return 0;
	}

	/* FALLBACK or for files */
	if (ti->tracknr != 1)
		return EIO;

	/* create fake ti */
	ti->sessionnr  = 1;
	ti->track_mode = 0;
	ti->data_mode  = 0;
	ti->flags = MMC_TRACKINFO_LRA_VALID | MMC_TRACKINFO_NWA_VALID;
	ti->track_start    = 0;
	ti->packet_size    = 32;
	ti->track_size    = di->last_possible_lba;
	ti->next_writable = di->last_possible_lba;
	ti->last_recorded = ti->next_writable;
	ti->free_blocks   = 0;

	return 0;
}

/* --------------------------------------------------------------------- */

static void udf_dump_discinfo(struct mmc_discinfo *di) {
  /* No-op for now */
}

static int udf_opendisc(const char *name, int flags) {
  dev_fd = open(name, flags);
  return (dev_fd == -1);
}

static void udf_closedisc(void) {
  if (dev_fd != -1) {
    close(dev_fd);
    dev_fd = -1;
  }
}

static int udf_update_discinfo(void) {
  if (dev_fd == -1)
    return EBADF;
  return ioctl(dev_fd, MMCGETDISCINFO, &mmc_discinfo);
}

static int udf_prepare_disc(void) {
  return 0;
}

static void udf_allow_writing(void) {
}

/* --------------------------------------------------------------------- */

static int udf_prepare_format_track512(void) {
  struct mmc_trackinfo ti;
  struct mmc_op op;
  int error;

  if (!(format_flags & FORMAT_TRACK512))
    return 0;

  /* get last track (again) */
  ti.tracknr = mmc_discinfo.last_track_last_session;
  error = udf_update_trackinfo(&mmc_discinfo, &ti);
  if (error)
    return error;

  /* Split up the space at 512 for iso cd9660 hooking */
  memset(&op, 0, sizeof(op));
  op.operation = MMC_OP_RESERVETRACK_NWA; /* UPTO nwa */
  op.mmc_profile = mmc_discinfo.mmc_profile;
  op.extent = 512; /* size */
  error = ioctl(dev_fd, MMCOP, &op);

  return error;
}

/* --------------------------------------------------------------------- */

static int udf_do_newfs(void) {
  int error;

  error = udf_do_newfs_prefix();
  if (error)
    return error;
  error = udf_do_rootdir();
  if (error)
    return error;
  error = udf_do_newfs_postfix();

  return error;
}

/* --------------------------------------------------------------------- */

static void usage(void) {
  (void)fprintf(stderr,
                "Usage: %s [-cFM] [-L loglabel] "
                "[-P discid] [-S sectorsize] [-s size] [-p perc] "
                "[-t gmtoff] [-v min_udf] [-V max_udf] special\n",
                getprogname());
  exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
  struct tm *tm;
  time_t now;
  off_t setsize;
  char scrap[255], *colon;
  int ch, req_enable, req_disable;
  int error;

  setprogname(argv[0]);

  /* initialise */
  format_str = strdup("");
  req_enable = req_disable = 0;
  setsize = 0;

  emul_mmc_profile = -1; /* invalid->no emulation	*/
  emul_packetsize = 1;   /* reasonable default		*/
  emul_sectorsize = 512; /* minimum allowed sector size	*/
  emul_size = 0;         /* empty			*/

  meta_fract = (float) UDF_META_PERC / 100.0;

  srandom((unsigned long)time(NULL));
  udf_init_create_context();
  context.app_name = "*NetBSD UDF";
  context.app_version_main = APP_VERSION_MAIN;
  context.app_version_sub = APP_VERSION_SUB;
  context.impl_name = IMPL_NAME;

  /* minimum and maximum UDF versions we advise */
  context.min_udf = 0x201;
  context.max_udf = 0x250;

  /* use user's time zone as default */
  (void)time(&now);
  tm = localtime(&now);
  context.gmtoff = tm->tm_gmtoff;

  /* process options */
  while ((ch = getopt(argc, argv, "cFL:Mp:P:s:S:B:t:v:V:")) != -1) {
    switch (ch) {
    case 'c':
      check_surface = 1;
      break;
    case 'F':
      /* create_new_session = 1; */
      break;
    case 'L':
      if (context.logvol_name)
        free(context.logvol_name);
      context.logvol_name = strdup(optarg);
      break;
    case 'M':
      req_disable |= FORMAT_META;
      break;
    case 'p':
      meta_fract = (float) a_num(optarg, "meta_perc") / 100.0;
      /* limit to `sensible` values */
      meta_fract = MIN(meta_fract, 0.99f);
      meta_fract = MAX(meta_fract, 0.01f);
      break;
    case 'v':
      context.min_udf = a_udf_version(optarg, "min_udf");
      if (context.min_udf > context.max_udf)
        context.max_udf = context.min_udf;
      break;
    case 'V':
      context.max_udf = a_udf_version(optarg, "max_udf");
      if (context.min_udf > context.max_udf)
        context.max_udf = context.max_udf;
      break;
    case 'P':
      /* check if there is a ':' in the name */
      if ((colon = strstr(optarg, ":"))) {
        if (context.volset_name)
          free(context.volset_name);
        *colon = '\0';
        context.volset_name = strdup(optarg);
        optarg = colon + 1;
      }
      if (context.primary_name)
        free(context.primary_name);
      if ((strstr(optarg, ":")))
        errx(1, "primary name can't have ':' in its name");
      context.primary_name = strdup(optarg);
      break;
    case 's':
      if (dehumanize_number(optarg, &setsize) < 0)
        errx(1, "can't parse size argument");
      setsize = MAX(0, setsize);
      break;
    case 'S':
      emul_sectorsize = a_num(optarg, "secsize");
      emul_sectorsize = MAX(512, emul_sectorsize);
      break;
    case 'B':
      emul_packetsize = a_num(optarg, "blockingnr, packetsize");
      emul_packetsize = MAX(emul_packetsize, 1);
      emul_packetsize = MIN(emul_packetsize, 32);
      break;
    case 't':
      /* time zone override */
      context.gmtoff = a_num(optarg, "gmtoff");
      break;
    default:
      usage();
      /* NOTREACHED */
    }
  }

  if (optind + 1 != argc)
    usage();

  /* get device specifier */
  dev_name = argv[optind];

  emul_size = setsize;
  error = udf_opendisc(dev_name, O_RDWR | O_CREAT | O_TRUNC);
  if (error) {
    udf_closedisc();
    errx(1, "can't open device");
  }

  /* get 'disc' information */
  error = udf_update_discinfo();
  if (error) {
    udf_closedisc();
    errx(1, "can't retrieve discinfo");
  }

  /* derive disc identifiers when not specified and check given */
  error = udf_proces_names();
  if (error) {
    /* error message has been printed */
    udf_closedisc();
    errx(1, "bad names given");
  }

  /* derive newfs disc format from disc profile */
  error = udf_derive_format(req_enable, req_disable, 0);
  if (error) {
    /* error message has been printed */
    udf_closedisc();
    errx(1, "can't derive format from media/settings");
  }

  udf_dump_discinfo(&mmc_discinfo);
  printf("Formatting disc compatible with UDF version %x to %x\n\n",
         context.min_udf, context.max_udf);

  /* sync context flags with global flags */
  context.format_flags = format_flags;

  (void)snprintb(scrap, sizeof(scrap), FORMAT_FLAGBITS,
                 (uint64_t)format_flags);
  printf("UDF properties       %s\n", scrap);
  printf("Volume set          `%s'\n", context.volset_name);
  printf("Primary volume      `%s`\n", context.primary_name);
  printf("Logical volume      `%s`\n", context.logvol_name);
  if (format_flags & FORMAT_META)
    printf("Metadata percentage  %d %%\n", (int) (meta_fract * 100.0f));
  printf("\n");

  /* prepare disc if necessary (recordables mainly) */
  error = udf_prepare_disc();
  if (error) {
    udf_closedisc();
    errx(1, "preparing disc failed");
  }
  error = udf_prepare_format_track512();
  if (error) {
    udf_closedisc();
    errx(1, "reservation for track512 failed");
  }

  /* perform the newfs itself */
  udf_allow_writing();
  error = udf_do_newfs();
  udf_closedisc();

  if (error)
    return EXIT_FAILURE;

  return EXIT_SUCCESS;
}
