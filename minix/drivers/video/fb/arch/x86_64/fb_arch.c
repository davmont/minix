/*
 * Architecture-dependent part of the framebuffer driver for x86_64 UEFI.
 *
 * There is no mode-setting hardware to program here: the boot loader (or
 * firmware) picked a GOP mode and the kernel captured the linear-framebuffer
 * geometry from the Multiboot2 framebuffer tag into kinfo (fb_addr, fb_pitch,
 * fb_width, fb_height, fb_bpp, fb_type; see kernel/arch/x86_64/pre_init.c).
 * This backend simply maps that framebuffer and reports its fixed geometry.
 *
 * The TTY console renders text into the same framebuffer (see
 * drivers/tty/tty/arch/i386/console.c).  Both map the same physical range;
 * pixels written through /dev/fb0 overdraw the text console and the next
 * console update overdraws them back.  That is the intended PoC coexistence
 * model - there is no display arbitration yet.
 *
 * EDID information is not used: the mode is whatever the firmware set up,
 * so the info argument to arch_fb_init() is ignored.
 */

#include <minix/chardriver.h>
#include <minix/drivers.h>
#include <minix/fb.h>
#include <minix/param.h>	/* struct kinfo (fb_* fields) */
#include <minix/type.h>
#include <minix/vm.h>
#include <minix/log.h>
#include <lib.h>		/* get_minix_kerninfo() */
#include <assert.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dev/videomode/videomode.h>
#include <dev/videomode/edidvar.h>
#include "fb.h"

/* globals */
static vir_bytes fb_vir;
static size_t fb_size;
static int initialized = 0;

static struct fb_fix_screeninfo gop_fbfs;
static struct fb_var_screeninfo gop_fbvs;

/* logging - use with log_warn(), log_info(), log_debug(), log_trace() */
static struct log log = {
	.name = "fb",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

int
arch_get_device(int minor, struct device *dev)
{
	if (!initialized) return ENXIO;
	if (minor != 0) return ENXIO;
	dev->dv_base = fb_vir;
	dev->dv_size = fb_size;
	return OK;
}

int
arch_get_varscreeninfo(int minor, struct fb_var_screeninfo *fbvsp)
{
	if (!initialized) return ENXIO;
	if (minor != 0) return ENXIO;

	*fbvsp = gop_fbvs;
	return OK;
}

int
arch_put_varscreeninfo(int minor, struct fb_var_screeninfo *fbvsp)
{
	assert(fbvsp != NULL);

	if (!initialized) return ENXIO;
	if (minor != 0) return ENXIO;

	/*
	 * The GOP mode is fixed by the firmware and the whole (single)
	 * frame is mapped: no mode switching and no panning.  Accept
	 * a no-op update, reject anything else.
	 */
	if (fbvsp->xoffset != 0 || fbvsp->yoffset != 0)
		return EINVAL;

	return OK;
}

int
arch_get_fixscreeninfo(int minor, struct fb_fix_screeninfo *fbfsp)
{
	if (!initialized) return ENXIO;
	if (minor != 0) return ENXIO;

	*fbfsp = gop_fbfs;
	return OK;
}

int
arch_pan_display(int minor, struct fb_var_screeninfo *fbvsp)
{
	return arch_put_varscreeninfo(minor, fbvsp);
}

int
arch_fb_init(int minor, struct edid_info *UNUSED(info))
{
	struct minix_kerninfo *ki;
	struct minix_mem_range mr;
	phys_bytes fb_phys;

	if (minor != 0) return ENXIO;	/* We support only one minor */

	if (initialized) return OK;

	ki = get_minix_kerninfo();
	if (ki == NULL || ki->kinfo == NULL || ki->kinfo->fb_addr == 0) {
		log_warn(&log,
		    "no boot framebuffer (BIOS boot?); GOP fb requires UEFI\n");
		return ENXIO;
	}
	if (ki->kinfo->fb_bpp != 32) {
		log_warn(&log, "unsupported framebuffer depth %u bpp\n",
		    ki->kinfo->fb_bpp);
		return ENXIO;
	}

	fb_phys = ki->kinfo->fb_addr;
	fb_size = (size_t)ki->kinfo->fb_pitch * ki->kinfo->fb_height;

	/* Ask the kernel for access to the framebuffer range, then map it. */
	mr.mr_base = fb_phys;
	mr.mr_limit = fb_phys + fb_size - 1;
	if (sys_privctl(SELF, SYS_PRIV_ADD_MEM, &mr) != OK) {
		log_warn(&log, "unable to request framebuffer access\n");
		return ENXIO;
	}

	fb_vir = (vir_bytes) vm_map_phys(SELF, (void *) fb_phys, fb_size);
	if (fb_vir == (vir_bytes) MAP_FAILED) {
		log_warn(&log, "unable to map framebuffer\n");
		return ENXIO;
	}

	/* fb_fix_screeninfo: fixed GOP geometry. */
	memset(&gop_fbfs, 0, sizeof(gop_fbfs));
	strlcpy(gop_fbfs.id, "gop_fb", sizeof(gop_fbfs.id));
	gop_fbfs.xpanstep = 0;
	gop_fbfs.ypanstep = 0;
	gop_fbfs.ywrapstep = 0;
	gop_fbfs.line_length = ki->kinfo->fb_pitch;
	gop_fbfs.mmio_start = 0;
	gop_fbfs.mmio_len = 0;

	/*
	 * fb_var_screeninfo: single fixed mode.  QEMU/OVMF GOP is BGRX
	 * (blue at bit 0), matching the bitfield layout below; a stricter
	 * implementation would decode the Multiboot2 color-info fields.
	 */
	memset(&gop_fbvs, 0, sizeof(gop_fbvs));
	gop_fbvs.xres = ki->kinfo->fb_width;
	gop_fbvs.yres = ki->kinfo->fb_height;
	gop_fbvs.xres_virtual = ki->kinfo->fb_pitch / 4;
	gop_fbvs.yres_virtual = ki->kinfo->fb_height;
	gop_fbvs.xoffset = 0;
	gop_fbvs.yoffset = 0;
	gop_fbvs.bits_per_pixel = 32;
	gop_fbvs.blue.offset = 0;   gop_fbvs.blue.length = 8;
	gop_fbvs.green.offset = 8;  gop_fbvs.green.length = 8;
	gop_fbvs.red.offset = 16;   gop_fbvs.red.length = 8;
	gop_fbvs.transp.offset = 24; gop_fbvs.transp.length = 8;

	initialized = 1;

	log_info(&log, "GOP framebuffer %ux%u, %u bpp, pitch %u, size %zu\n",
	    ki->kinfo->fb_width, ki->kinfo->fb_height, ki->kinfo->fb_bpp,
	    ki->kinfo->fb_pitch, fb_size);

	return OK;
}
