/*	$NetBSD$	*/
/*
 * libfbgui - a minimal framebuffer graphics library for MINIX.
 *
 * Ties together the userland graphics stack that has no display server:
 *   - output  : /dev/fb0 (the UEFI GOP / OMAP linear framebuffer),
 *   - drawing : pixman (2D compositing) on an off-screen back buffer,
 *   - text    : FreeType (anti-aliased glyphs composited via pixman),
 *   - input   : /dev/mousemux (PS/2 mouse through the input server).
 *
 * The point of the library is a cheap present path: callers draw into an
 * off-screen pixman surface and mark the changed rectangles as damage;
 * fbgui_present() copies only those rows to the framebuffer instead of
 * blitting the whole (multi-megabyte) frame every time.
 */
#ifndef _FBGUI_H_
#define _FBGUI_H_

#include <pixman.h>

typedef struct fbgui fbgui_t;

/* Open /dev/fb0, read its geometry, and allocate the back buffer.
 * Returns NULL on failure (no framebuffer, non-32bpp mode, no memory). */
fbgui_t *fbgui_open(void);
void	 fbgui_close(fbgui_t *g);

int	 fbgui_width(const fbgui_t *g);
int	 fbgui_height(const fbgui_t *g);

/* The back-buffer image (PIXMAN_x8r8g8b8, full screen size).  Draw into it
 * with pixman directly, or use the helpers below. */
pixman_image_t *fbgui_surface(fbgui_t *g);

/* Convenience: solid-fill a rectangle in the back buffer (no auto-damage). */
void	 fbgui_fill_rect(fbgui_t *g, const pixman_color_t *c,
	    int x, int y, int w, int h);

/* Text.  fbgui_load_font() must succeed before drawing text; returns 0 on
 * success, -1 otherwise.  fbgui_draw_text() composites an ASCII string with
 * the pen baseline at (x,y).  fbgui_text_width() returns the advance width. */
int	 fbgui_load_font(fbgui_t *g, const char *path);
void	 fbgui_draw_text(fbgui_t *g, int x, int y, const char *s,
	    const pixman_color_t *color, int px_size);
int	 fbgui_text_width(fbgui_t *g, const char *s, int px_size);

/* Damage tracking.  Mark a rectangle of the back buffer as changed. */
void	 fbgui_damage(fbgui_t *g, int x, int y, int w, int h);
void	 fbgui_damage_all(fbgui_t *g);

/* Copy the damaged rows to the framebuffer and clear damage.  Returns the
 * number of bytes written (the cost of the frame).  present_full() pushes
 * the whole framebuffer, for the initial frame. */
long	 fbgui_present(fbgui_t *g);
long	 fbgui_present_full(fbgui_t *g);

/* Input.  fbgui_open_mouse() returns a non-blocking fd for the mouse, or -1.
 * fbgui_read_mouse() drains all pending events, accumulating relative motion
 * into dx and dy and the current button bitmap into buttons (bit 0 = button
 * 1).  Returns the number of events consumed. */
int	 fbgui_open_mouse(void);
int	 fbgui_read_mouse(int fd, int *dx, int *dy, int *buttons);

#endif /* _FBGUI_H_ */
