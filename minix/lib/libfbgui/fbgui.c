/*	$NetBSD$	*/
/*
 * libfbgui - minimal framebuffer graphics library for MINIX.  See fbgui.h.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <minix/fb.h>
#include <sys/ioc_fb.h>
#include <minix/input.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pixman.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include "fbgui.h"

#define FBGUI_MAX_DAMAGE	64

struct rect { int x, y, w, h; };

struct fbgui {
	int		fbfd;		/* /dev/fb0 */
	int		w, h;		/* screen geometry (pixels) */
	int		line;		/* framebuffer stride (bytes/row) */
	int		stride;		/* back-buffer stride (bytes/row) */
	uint32_t       *back;		/* back-buffer pixels */
	pixman_image_t *surface;	/* pixman view of the back buffer */

	FT_Library	ftlib;
	FT_Face		face;
	int		have_font;

	struct rect	damage[FBGUI_MAX_DAMAGE];
	int		ndamage;
	int		full;		/* whole screen is damaged */
};

/* --- lifecycle --- */

fbgui_t *
fbgui_open(void)
{
	fbgui_t *g;
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;

	if ((g = calloc(1, sizeof(*g))) == NULL)
		return NULL;

	if ((g->fbfd = open("/dev/fb0", O_RDWR)) < 0)
		goto fail;
	if (ioctl(g->fbfd, FBIOGET_VSCREENINFO, &var) < 0)
		goto fail;
	if (ioctl(g->fbfd, FBIOGET_FSCREENINFO, &fix) < 0)
		goto fail;
	if (var.bits_per_pixel != 32)
		goto fail;	/* only 32bpp is supported for now */

	g->w = var.xres;
	g->h = var.yres;
	g->line = fix.line_length;
	g->stride = g->w * 4;

	if ((g->back = malloc((size_t)g->stride * g->h)) == NULL)
		goto fail;
	memset(g->back, 0, (size_t)g->stride * g->h);

	g->surface = pixman_image_create_bits(PIXMAN_x8r8g8b8, g->w, g->h,
	    g->back, g->stride);
	if (g->surface == NULL)
		goto fail;

	g->full = 1;	/* first present should push everything */
	return g;

fail:
	fbgui_close(g);
	return NULL;
}

void
fbgui_close(fbgui_t *g)
{
	if (g == NULL)
		return;
	if (g->surface != NULL)
		pixman_image_unref(g->surface);
	if (g->have_font) {
		FT_Done_Face(g->face);
		FT_Done_FreeType(g->ftlib);
	}
	if (g->back != NULL)
		free(g->back);
	if (g->fbfd >= 0)
		close(g->fbfd);
	free(g);
}

int fbgui_width(const fbgui_t *g)		{ return g->w; }
int fbgui_height(const fbgui_t *g)		{ return g->h; }
pixman_image_t *fbgui_surface(fbgui_t *g)	{ return g->surface; }

/* --- drawing helpers --- */

void
fbgui_fill_rect(fbgui_t *g, const pixman_color_t *c, int x, int y, int w, int h)
{
	pixman_box32_t box = { x, y, x + w, y + h };
	pixman_region32_t r;
	pixman_image_t *s;

	pixman_region32_init_rects(&r, &box, 1);
	pixman_image_set_clip_region32(g->surface, &r);
	s = pixman_image_create_solid_fill(c);
	pixman_image_composite32(PIXMAN_OP_SRC, s, NULL, g->surface,
	    0, 0, 0, 0, 0, 0, g->w, g->h);
	pixman_image_unref(s);
	pixman_image_set_clip_region32(g->surface, NULL);
	pixman_region32_fini(&r);
}

/* --- text --- */

int
fbgui_load_font(fbgui_t *g, const char *path)
{
	if (g->have_font) {
		FT_Done_Face(g->face);
		FT_Done_FreeType(g->ftlib);
		g->have_font = 0;
	}
	if (FT_Init_FreeType(&g->ftlib) != 0)
		return -1;
	if (FT_New_Face(g->ftlib, path, 0, &g->face) != 0) {
		FT_Done_FreeType(g->ftlib);
		return -1;
	}
	g->have_font = 1;
	return 0;
}

void
fbgui_draw_text(fbgui_t *g, int x, int y, const char *s,
	const pixman_color_t *color, int px_size)
{
	pixman_image_t *fg;
	const char *p;

	if (!g->have_font)
		return;

	FT_Set_Pixel_Sizes(g->face, 0, px_size);
	fg = pixman_image_create_solid_fill(color);

	for (p = s; *p != '\0'; p++) {
		FT_GlyphSlot gl;
		FT_Bitmap *bm;

		if (FT_Load_Char(g->face, (unsigned char)*p, FT_LOAD_RENDER) != 0)
			continue;
		gl = g->face->glyph;
		bm = &gl->bitmap;
		if (bm->width != 0 && bm->rows != 0 && bm->buffer != NULL) {
			/*
			 * Stage the glyph at a stride pixman will accept.
			 *
			 * FreeType hands back an 8-bit bitmap whose pitch is the
			 * glyph width rounded to a byte, and whose buffer has no
			 * particular alignment.  pixman requires the stride to be
			 * a multiple of 4 and the base to be uint32_t-aligned, so
			 * handing it FreeType's buffer directly makes
			 * pixman_image_create_bits() fail -- it returns NULL, the
			 * glyph is quietly dropped, and text simply never appears.
			 * (It went unnoticed because a missing font drops text
			 * too, and the default font path was wrong.)
			 */
			int src_pitch = (bm->pitch > 0) ? bm->pitch : -bm->pitch;
			int ms = ((int)bm->width + 3) & ~3;
			unsigned char *gbuf = malloc((size_t)ms * bm->rows);

			if (gbuf != NULL) {
				pixman_image_t *m;
				unsigned int r;

				memset(gbuf, 0, (size_t)ms * bm->rows);
				for (r = 0; r < bm->rows; r++)
					memcpy(gbuf + (size_t)r * ms,
					    bm->buffer + (size_t)r * src_pitch,
					    bm->width);

				m = pixman_image_create_bits(PIXMAN_a8,
				    bm->width, bm->rows, (uint32_t *)gbuf, ms);
				if (m != NULL) {
					pixman_image_composite32(PIXMAN_OP_OVER,
					    fg, m, g->surface, 0, 0, 0, 0,
					    x + gl->bitmap_left,
					    y - gl->bitmap_top,
					    bm->width, bm->rows);
					pixman_image_unref(m);
				}
				free(gbuf);
			}
		}
		x += gl->advance.x >> 6;
	}
	pixman_image_unref(fg);
}

int
fbgui_text_width(fbgui_t *g, const char *s, int px_size)
{
	const char *p;
	int w = 0;

	if (!g->have_font)
		return 0;
	FT_Set_Pixel_Sizes(g->face, 0, px_size);
	for (p = s; *p != '\0'; p++) {
		if (FT_Load_Char(g->face, (unsigned char)*p, FT_LOAD_DEFAULT) != 0)
			continue;
		w += g->face->glyph->advance.x >> 6;
	}
	return w;
}

/* --- damage tracking --- */

static int
rects_touch(const struct rect *a, const struct rect *b)
{
	/* True if a and b overlap or abut (so merging avoids a seam). */
	return !(b->x > a->x + a->w || b->x + b->w < a->x ||
	    b->y > a->y + a->h || b->y + b->h < a->y);
}

static void
rect_merge(struct rect *a, const struct rect *b)
{
	int x0 = a->x < b->x ? a->x : b->x;
	int y0 = a->y < b->y ? a->y : b->y;
	int x1 = a->x + a->w > b->x + b->w ? a->x + a->w : b->x + b->w;
	int y1 = a->y + a->h > b->y + b->h ? a->y + a->h : b->y + b->h;

	a->x = x0;
	a->y = y0;
	a->w = x1 - x0;
	a->h = y1 - y0;
}

void
fbgui_damage(fbgui_t *g, int x, int y, int w, int h)
{
	struct rect nr;
	int i;

	if (g->full)
		return;

	/* Clip to screen. */
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > g->w) w = g->w - x;
	if (y + h > g->h) h = g->h - y;
	if (w <= 0 || h <= 0)
		return;

	nr.x = x; nr.y = y; nr.w = w; nr.h = h;

	/* Merge into an existing rectangle it touches, to keep the list short. */
	for (i = 0; i < g->ndamage; i++) {
		if (rects_touch(&g->damage[i], &nr)) {
			rect_merge(&g->damage[i], &nr);
			return;
		}
	}
	if (g->ndamage < FBGUI_MAX_DAMAGE) {
		g->damage[g->ndamage++] = nr;
	} else {
		g->full = 1;	/* too fragmented; just repaint everything */
	}
}

void
fbgui_damage_all(fbgui_t *g)
{
	g->full = 1;
	g->ndamage = 0;
}

/* --- present --- */

static long
present_rect(fbgui_t *g, int x, int y, int w, int h)
{
	long bytes = 0;
	int row;

	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > g->w) w = g->w - x;
	if (y + h > g->h) h = g->h - y;
	if (w <= 0 || h <= 0)
		return 0;

	for (row = y; row < y + h; row++) {
		off_t off = (off_t)row * g->line + (off_t)x * 4;
		const char *src = (const char *)g->back +
		    (size_t)row * g->stride + (size_t)x * 4;
		size_t n = (size_t)w * 4;

		if (lseek(g->fbfd, off, SEEK_SET) < 0)
			break;
		if (write(g->fbfd, src, n) != (ssize_t)n)
			break;
		bytes += (long)n;
	}
	return bytes;
}

long
fbgui_present_full(fbgui_t *g)
{
	long bytes = present_rect(g, 0, 0, g->w, g->h);

	g->full = 0;
	g->ndamage = 0;
	return bytes;
}

long
fbgui_present(fbgui_t *g)
{
	long bytes = 0;
	int i;

	if (g->full)
		return fbgui_present_full(g);

	for (i = 0; i < g->ndamage; i++) {
		bytes += present_rect(g, g->damage[i].x, g->damage[i].y,
		    g->damage[i].w, g->damage[i].h);
	}
	g->ndamage = 0;
	return bytes;
}

/* --- input --- */

int
fbgui_open_mouse(void)
{
	int fd = open("/dev/mousemux", O_RDONLY | O_NONBLOCK);

	if (fd < 0)
		fd = open("/dev/mouse0", O_RDONLY | O_NONBLOCK);
	return fd;
}

int
fbgui_read_mouse(int fd, int *dx, int *dy, int *buttons)
{
	struct input_event ev;
	int count = 0;

	if (fd < 0)
		return 0;

	while (read(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
		if (ev.page == INPUT_PAGE_GD && ev.code == INPUT_GD_X)
			*dx += ev.value;
		else if (ev.page == INPUT_PAGE_GD && ev.code == INPUT_GD_Y)
			*dy += ev.value;
		else if (ev.page == INPUT_PAGE_BUTTON &&
		    ev.code == INPUT_BUTTON_1) {
			if (ev.value)
				*buttons |= 1;
			else
				*buttons &= ~1;
		}
		count++;
	}
	return count;
}
