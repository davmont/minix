/*	wlcompd - a Wayland compositor for MINIX
 *
 * Owns /dev/fb0 through libfbgui (pixman to composite, FreeType for the title
 * bars) and speaks Wayland to its clients: wl_compositor, wl_subcompositor,
 * wl_shm, wl_seat, wl_output, wl_data_device_manager and xdg_shell.
 *
 * Everything underneath it has been proved separately, on-target: a client's
 * pixels arrive as a POSIX shm pool (wlprobe), every request is dispatched
 * through libffi (ffiprobe), the event loop runs on MINIX's poll(2) emulation
 * of epoll/timerfd/signalfd/eventfd (wlcoreprobe), and keys are translated with
 * libxkbcommon against the keymap we ship (xkbprobe).
 *
 * ---------------------------------------------------------------------------
 * Surfaces are a TREE, not a list.  This is the shape a real toolkit needs and
 * the reason for most of what follows:
 *
 *   - a toplevel is a window; it has an absolute position on screen,
 *   - a popup (a menu, a tooltip) hangs off a parent at an offset its
 *     xdg_positioner works out, and
 *   - a subsurface (Qt and GTK both use them, for decorations and for video
 *     panes) hangs off a parent at an offset the client sets.
 *
 * So position is relative to the parent and resolved by walking up, rendering
 * is a depth-first walk in z-order, and hit-testing is the same walk backwards.
 * A flat list cannot express any of that.
 *
 * ---------------------------------------------------------------------------
 * Two MINIX-specific things worth knowing before reading further.
 *
 * Keycodes.  MINIX's input server speaks USB HID usages; Wayland speaks evdev.
 * They are not the same and not a fixed offset apart (HID orders letters
 * alphabetically, evdev by QWERTY position), so every key goes through
 * hid_evdev.h.  Get this wrong and the keymap silently produces wrong letters.
 *
 * Client buffers.  A committed buffer is copied into memory of our own before
 * it is composited.  Compositing straight out of the client's pool would mean
 * rendering from memory another process can rewrite underneath us -- and, on
 * MINIX, that could not be defended with the usual SIGBUS guard, which needs
 * SA_SIGINFO (see the note in wayland-shm.c).
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <pixman.h>
#include <fbgui.h>
#include <minix/input.h>

#include <wayland-server.h>
#include <xkbcommon/xkbcommon.h>

#include "xdg-shell-server-protocol.h"
#include "wlr-layer-shell-server-protocol.h"
#include "wlr-foreign-toplevel-server-protocol.h"
#include "ext-workspace-server-protocol.h"
#include "xdg-decoration-server-protocol.h"
#include "hid_evdev.h"

#define KEYMAP_PATH	"/usr/share/xkb/us.xkb"

/*
 * wl_pointer.button carries an evdev button code, the same convention as the
 * keycodes.  <minix/input.h> has no name for it -- it thinks in HID -- so spell
 * it out.
 */
#define BTN_LEFT	0x110
#define BTN_RIGHT	0x111
#define BTN_MIDDLE	0x112

#define TITLEH		22		/* title bar height */
#define CLOSEW		TITLEH		/* square close box at its right */
#define RESIZEW		14		/* square resize grip, bottom-right */
#define MINW		80		/* smallest we will configure a window */
#define MINH		48
#define CURW		12		/* our fallback cursor */
#define CURH		19
#define REPAINT_MS	16		/* ~60 Hz cap on recompositing */

#define FONT_DEFAULT	"/usr/share/fonts/TTF/DejaVuSans.ttf"

/* ------------------------------------------------------------------ state */

enum role {
	ROLE_NONE = 0,
	ROLE_TOPLEVEL,
	ROLE_POPUP,
	ROLE_SUBSURFACE,
	ROLE_CURSOR,		/* a client's pointer cursor */
	ROLE_LAYER,		/* a panel, dock or wallpaper (wlr-layer-shell) */
};

#define NLAYERS	4		/* background, bottom, top, overlay */

/*
 * Virtual desktops.  Four, like every other desktop ever shipped; the number is
 * fixed because ext-workspace lets a client create workspaces only if we say we
 * can, and we do not.
 */
#define NWORKSPACES	4

struct positioner {
	int	w, h;			/* size of the popup */
	int	ax, ay, aw, ah;		/* anchor rect, in the parent */
	uint32_t anchor;
	uint32_t gravity;
	int	ox, oy;			/* offset */
};

struct surface {
	struct wl_resource	*resource;
	struct wl_resource	*xdg_surface;
	struct wl_resource	*xdg_toplevel;
	struct wl_resource	*xdg_popup;
	struct wl_resource	*subsurface;

	enum role		 role;
	struct surface		*parent;
	struct wl_list		 children;	/* struct surface, by sibling */
	struct wl_list		 sibling;	/* link in parent->children */

	/* Pending (attached but not yet committed) buffer. */
	struct wl_resource	*pending_buffer;
	int			 pending_buffer_set;

	/* Our own copy of the last committed buffer.  Never the client's. */
	uint32_t		*pix;
	pixman_image_t		*img;
	int			 w, h;

	/* Damage the client named since its last commit, surface-local. */
	pixman_region32_t	 damage;

	int			 cfg_w, cfg_h;	/* last size we configured */

	/* wlr-layer-shell state (ROLE_LAYER). */
	struct wl_resource	*layer_surface;
	uint32_t		 layer;		/* background..overlay */
	uint32_t		 lanchor;	/* zwlr anchor bitmask */
	int32_t			 lw, lh;	/* size the client asked for */
	int32_t			 lexclusive;	/* exclusive zone */
	int32_t			 lmargin[4];	/* top, right, bottom, left */
	uint32_t		 lkbd;		/* keyboard interactivity */
	int			 lconfigured;	/* first configure sent */
	struct wl_list		 llink;		/* link in C.layers[layer] */

	/*
	 * Position.  A toplevel's is absolute (its title bar's top-left); a
	 * popup's and a subsurface's is relative to the parent's content origin.
	 */
	int			 x, y;

	int			 hx, hy;	/* cursor hotspot (ROLE_CURSOR) */

	int			 mapped;
	char			 title[64];
	char			 app_id[64];

	/*
	 * wlr-foreign-toplevel state (ROLE_TOPLEVEL).  A minimized toplevel is
	 * still mapped -- the client knows nothing about it -- we simply stop
	 * drawing it and stop hitting it with the pointer.  Maximizing remembers
	 * the floating geometry so unmaximize can put it back.
	 */
	int			 minimized;
	int			 maximized;
	int			 ws;		/* the desktop it lives on */
	int			 mx, my, mw, mh;	/* geometry before maximize */
	struct wl_list		 ftl;		/* struct ftl, one per manager */

	struct wl_list		 frame_callbacks;
	struct wl_list		 link;		/* C.toplevels, if a toplevel */
};

static struct {
	struct wl_display	*display;
	struct wl_event_loop	*loop;
	fbgui_t			*fb;

	struct wl_list		 toplevels;	/* bottom-to-top */
	struct wl_list		 layers[NLAYERS];	/* wlr-layer-shell */
	struct surface		*focus;

	struct wl_list		 outputs;	/* wl_output resources */
	struct wl_list		 ftl_managers;	/* foreign-toplevel managers */

	int			 ws;		/* the desktop on screen now */
	struct wl_list		 ws_managers;	/* struct wsmgr */

	/* Seat */
	struct wl_list		 pointers;
	struct wl_list		 keyboards;
	struct wl_list		 data_devices;
	int			 cx, cy;
	int			 buttons;
	struct surface		*ptr_focus;

	/* The client's cursor, if it set one; otherwise we draw our own. */
	struct surface		*cursor;

	/* Selection (the clipboard): the offering client's data source. */
	struct wl_resource	*selection;

	/* Keyboard */
	struct xkb_context	*xkb;
	struct xkb_keymap	*keymap;
	struct xkb_state	*xkb_state;
	int			 keymap_fd;
	size_t			 keymap_size;

	struct surface		*dragging;
	int			 drag_dx, drag_dy;

	struct surface		*resizing;
	int			 rz_cx0, rz_cy0;
	int			 rz_w0, rz_h0;

	pixman_region32_t	 damage;	/* screen damage for this frame */
	struct wl_event_source	*repaint;
} C;

static int toplevel_visible(const struct surface *);

/*
 * wlr-foreign-toplevel notifications.  Defined further down with the rest of the
 * protocol, but the places that must fire them -- map, destroy, focus, set_title
 * -- come first.
 */
static void ftl_announce(struct surface *);
static void ftl_closed(struct surface *);
static void ftl_focus_changed(void);
static void ftl_state_changed(struct surface *);
static void ftl_title_changed(struct surface *);
static void ftl_app_id_changed(struct surface *);


static FILE *lg;

static void
wlog(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(lg, fmt, ap);
	va_end(ap);
	fflush(lg);
}

static uint32_t
now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* --------------------------------------------------------------- geometry */

/*
 * Where a surface's *content* starts on screen.  A toplevel's content sits
 * below its title bar; a popup's and a subsurface's is an offset from the
 * parent's content origin, so this walks up the tree.
 */
static void
surface_origin(const struct surface *s, int *ox, int *oy)
{
	if (s->role == ROLE_LAYER) {
		/* A panel has no decoration: its content starts where it sits. */
		*ox = s->x;
		*oy = s->y;
		return;
	}
	if (s->role == ROLE_TOPLEVEL || s->parent == NULL) {
		*ox = s->x;
		*oy = s->y + TITLEH;
		return;
	}
	surface_origin(s->parent, ox, oy);
	*ox += s->x;
	*oy += s->y;
}

/* ------------------------------------------------------------- rendering */

static const pixman_color_t c_desk  = { 0x1c1c, 0x2222, 0x2a2a, 0xffff };
static const pixman_color_t c_title = { 0x3a3a, 0x5a5a, 0x8888, 0xffff };
static const pixman_color_t c_tfoc  = { 0x4a4a, 0x8a8a, 0xcccc, 0xffff };
static const pixman_color_t c_ttext = { 0xffff, 0xffff, 0xffff, 0xffff };
static const pixman_color_t c_cur   = { 0xffff, 0xffff, 0xffff, 0xffff };
static const pixman_color_t c_curb  = { 0x0000, 0x0000, 0x0000, 0xffff };
static const pixman_color_t c_grip  = { 0x8888, 0x9999, 0xaaaa, 0xffff };

/*
 * Fallback pointer, used when a client has not set its own cursor (no XCursor
 * theme is installed here, so that is the common case).  A white arrow with a
 * black outline, so it stays visible over both the dark wallpaper and the light
 * panels/menus -- unlike the old plain white block ('X' = black outline,
 * '.' = white fill, ' ' = transparent).  Hotspot is the top-left tip.
 */
#define CURSOR_W 12
#define CURSOR_H 19
static const char *const cursor_bits[CURSOR_H] = {
	"X",
	"XX",
	"X.X",
	"X..X",
	"X...X",
	"X....X",
	"X.....X",
	"X......X",
	"X.......X",
	"X........X",
	"X.........X",
	"X......XXXX",
	"X...X..X",
	"X..XX..X",
	"X.X X..X",
	"XX  X..X",
	"X    X..X",
	"     X..X",
	"     XXXX",
};

/*
 * Damage is a region, not a bounding box.  A box is fine for one window and a
 * cursor, but a menu opening across the screen from a blinking caret would
 * union into a box covering both -- and everything between.
 */
static void
dmg_add(int x, int y, int w, int h)
{
	if (w <= 0 || h <= 0)
		return;
	pixman_region32_union_rect(&C.damage, &C.damage, x, y, w, h);
}

static void
dmg_add_all(void)
{
	dmg_add(0, 0, fbgui_width(C.fb), fbgui_height(C.fb));
}

/* A surface's whole on-screen extent, itself and everything hanging off it. */
static void
dmg_add_surface(const struct surface *s)
{
	const struct surface *c;
	int ox, oy;

	if (s->w <= 0 || s->h <= 0)
		return;

	surface_origin(s, &ox, &oy);
	if (s->role == ROLE_TOPLEVEL)
		dmg_add(s->x, s->y, s->w, TITLEH + s->h);	/* with the bar */
	else
		dmg_add(ox, oy, s->w, s->h);

	wl_list_for_each(c, &s->children, sibling)
		if (c->mapped)
			dmg_add_surface(c);
}

static void
dmg_add_cursor(int x, int y)
{
	if (C.cursor != NULL && C.cursor->mapped && C.cursor->w > 0)
		dmg_add(x - C.cursor->hx, y - C.cursor->hy,
		    C.cursor->w, C.cursor->h);
	else
		dmg_add(x, y, CURW, CURH);
}

/*
 * Fill a rectangle, but only the parts of it that are damaged.
 *
 * This cannot simply set a clip region and let fbgui_fill_rect() run: that
 * function installs a clip of its own and then clears it, so any clip we had
 * set would be silently dropped.  Intersect explicitly and fill the pieces.
 */
static void
fill_damaged(const pixman_color_t *c, int x, int y, int w, int h)
{
	pixman_region32_t r;
	pixman_box32_t *boxes;
	int i, n;

	if (w <= 0 || h <= 0)
		return;

	pixman_region32_init_rect(&r, x, y, w, h);
	pixman_region32_intersect(&r, &r, &C.damage);

	boxes = pixman_region32_rectangles(&r, &n);
	for (i = 0; i < n; i++)
		fbgui_fill_rect(C.fb, c, boxes[i].x1, boxes[i].y1,
		    boxes[i].x2 - boxes[i].x1, boxes[i].y2 - boxes[i].y1);

	pixman_region32_fini(&r);
}

/*
 * Bound the back buffer to the damage region.  pixman honours a destination
 * clip, which is what keeps a composited surface and its text inside the
 * damaged area -- fbgui_draw_text() has no clipping of its own, and a glyph
 * would otherwise be drawn outside and then never presented, leaving a stale
 * pixel behind on the next frame that does cover it.
 *
 * Must be re-applied after any fbgui_fill_rect(), which clears it.
 */
static void
clip_to_damage(void)
{
	pixman_image_set_clip_region32(fbgui_surface(C.fb), &C.damage);
}

static void
clip_off(void)
{
	pixman_image_set_clip_region32(fbgui_surface(C.fb), NULL);
}

static void render_surface(struct surface *s);
static void layer_send_configure(struct surface *s);
static void keyboard_focus(struct surface *s);

/* Draw a surface's content and then everything hanging off it, in order. */
static void
render_children(struct surface *s)
{
	struct surface *c;

	wl_list_for_each(c, &s->children, sibling)
		if (c->mapped)
			render_surface(c);
}

static void
render_surface(struct surface *s)
{
	int ox, oy;

	if (s->img == NULL || s->role == ROLE_CURSOR)
		return;

	surface_origin(s, &ox, &oy);

	if (s->role == ROLE_TOPLEVEL) {
		const pixman_color_t *tc =
		    (s == C.focus) ? &c_tfoc : &c_title;

		fill_damaged(tc, s->x, s->y, s->w, TITLEH);

		clip_to_damage();
		if (s->title[0] != '\0')
			fbgui_draw_text(C.fb, s->x + 8, s->y + 16, s->title,
			    &c_ttext, 14);
		fbgui_draw_text(C.fb, s->x + s->w - CLOSEW + 6, s->y + 16, "x",
		    &c_ttext, 14);
	} else {
		clip_to_damage();
	}

	pixman_image_composite32(PIXMAN_OP_SRC, s->img, NULL,
	    fbgui_surface(C.fb), 0, 0, 0, 0, ox, oy, s->w, s->h);
	clip_off();

	if (s->role == ROLE_TOPLEVEL)
		fill_damaged(&c_grip, s->x + s->w - RESIZEW,
		    s->y + TITLEH + s->h - RESIZEW, RESIZEW, RESIZEW);

	render_children(s);
}

static void
render_cursor(void)
{
	struct surface *cur = C.cursor;

	if (cur != NULL && cur->mapped && cur->img != NULL) {
		clip_to_damage();
		pixman_image_composite32(PIXMAN_OP_OVER, cur->img, NULL,
		    fbgui_surface(C.fb), 0, 0, 0, 0,
		    C.cx - cur->hx, C.cy - cur->hy, cur->w, cur->h);
		clip_off();
	} else {
		int cy, cx;
		for (cy = 0; cy < CURSOR_H; cy++) {
			const char *row = cursor_bits[cy];
			for (cx = 0; row[cx] != '\0'; cx++) {
				if (row[cx] == 'X')
					fill_damaged(&c_curb, C.cx + cx,
					    C.cy + cy, 1, 1);
				else if (row[cx] == '.')
					fill_damaged(&c_cur, C.cx + cx,
					    C.cy + cy, 1, 1);
			}
		}
	}
}

static void
recomposite(void)
{
	struct surface *s;
	pixman_box32_t *boxes;
	int i, n;

	if (!pixman_region32_not_empty(&C.damage))
		return;			/* nothing changed this frame */

	/* Clamp to the screen: a window dragged off the edge would otherwise
	 * damage coordinates the framebuffer does not have. */
	pixman_region32_intersect_rect(&C.damage, &C.damage, 0, 0,
	    fbgui_width(C.fb), fbgui_height(C.fb));
	if (!pixman_region32_not_empty(&C.damage))
		return;

	fill_damaged(&c_desk, 0, 0, fbgui_width(C.fb), fbgui_height(C.fb));

	/*
	 * Stacking order is the whole point of layers: a wallpaper goes under
	 * the windows, a panel over them, and a lock screen over everything.
	 */
	wl_list_for_each(s, &C.layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND],
	    llink)
		if (s->mapped)
			render_surface(s);
	wl_list_for_each(s, &C.layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM], llink)
		if (s->mapped)
			render_surface(s);

	wl_list_for_each(s, &C.toplevels, link)
		if (toplevel_visible(s))
			render_surface(s);

	wl_list_for_each(s, &C.layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP], llink)
		if (s->mapped)
			render_surface(s);
	wl_list_for_each(s, &C.layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY], llink)
		if (s->mapped)
			render_surface(s);

	render_cursor();

	boxes = pixman_region32_rectangles(&C.damage, &n);
	for (i = 0; i < n; i++)
		fbgui_damage(C.fb, boxes[i].x1, boxes[i].y1,
		    boxes[i].x2 - boxes[i].x1, boxes[i].y2 - boxes[i].y1);

	{
		static long frames, bytes;
		long full = (long)fbgui_width(C.fb) * fbgui_height(C.fb) * 4;

		bytes += fbgui_present(C.fb);
		if (++frames % 20 == 0)
			wlog("damage: %ld frames, %d rect(s), avg %ld KB/frame "
			    "(a full screen is %ld KB)\n", frames, n,
			    (bytes / frames) / 1024, full / 1024);
	}

	pixman_region32_clear(&C.damage);
}

static void
send_frame_callbacks_for(struct surface *s, uint32_t t)
{
	struct wl_resource *cb, *tmp;
	struct surface *c;

	wl_resource_for_each_safe(cb, tmp, &s->frame_callbacks) {
		wl_callback_send_done(cb, t);
		/*
		 * wl_resource_destroy() posts delete_id and frees the resource,
		 * but it does NOT take it out of whatever list the compositor
		 * put it in -- that link is ours to manage.  Leaving it behind
		 * meant the next repaint walked freed memory and "destroyed"
		 * whatever it found there, sending the client delete_id for ids
		 * it had never heard of.  Only a client that draws more than one
		 * frame ever notices; Qt does.
		 */
		wl_list_remove(wl_resource_get_link(cb));
		wl_resource_destroy(cb);
	}
	wl_list_for_each(c, &s->children, sibling)
		send_frame_callbacks_for(c, t);
}

static int
on_repaint(void *data)
{
	struct surface *s;
	uint32_t t = now_ms();
	int i;

	(void)data;

	recomposite();

	wl_list_for_each(s, &C.toplevels, link)
		send_frame_callbacks_for(s, t);
	for (i = 0; i < NLAYERS; i++)
		wl_list_for_each(s, &C.layers[i], llink)
			send_frame_callbacks_for(s, t);
	if (C.cursor != NULL)
		send_frame_callbacks_for(C.cursor, t);

	wl_event_source_timer_update(C.repaint, REPAINT_MS);
	return 0;
}

/* ------------------------------------------------------------- xdg config */

static void
send_configure(struct surface *s, int w, int h, int resizing)
{
	struct wl_array states;
	uint32_t *st;

	if (s->xdg_toplevel == NULL || s->xdg_surface == NULL)
		return;

	wl_array_init(&states);
	if ((st = wl_array_add(&states, sizeof(uint32_t))) != NULL)
		*st = XDG_TOPLEVEL_STATE_ACTIVATED;
	if (resizing && (st = wl_array_add(&states, sizeof(uint32_t))) != NULL)
		*st = XDG_TOPLEVEL_STATE_RESIZING;

	xdg_toplevel_send_configure(s->xdg_toplevel, w, h, &states);
	wl_array_release(&states);

	xdg_surface_send_configure(s->xdg_surface,
	    wl_display_next_serial(C.display));

	s->cfg_w = w;
	s->cfg_h = h;
}

/* -------------------------------------------------------------- wl_buffer */

static int
surface_take_buffer(struct surface *s, struct wl_resource *buffer)
{
	struct wl_shm_buffer *shm;
	uint32_t *src;
	int w, h, stride, y;

	if ((shm = wl_shm_buffer_get(buffer)) == NULL) {
		wlog("commit: not an shm buffer\n");
		return -1;
	}

	w = wl_shm_buffer_get_width(shm);
	h = wl_shm_buffer_get_height(shm);
	stride = wl_shm_buffer_get_stride(shm);

	if (w <= 0 || h <= 0 || w > 4096 || h > 4096)
		return -1;

	if (s->pix == NULL || s->w != w || s->h != h) {
		uint32_t *np = calloc((size_t)w * h, sizeof(uint32_t));

		if (np == NULL)
			return -1;
		if (s->img != NULL)
			pixman_image_unref(s->img);
		free(s->pix);
		s->pix = np;
		s->w = w;
		s->h = h;
		/*
		 * a8r8g8b8, not x8r8g8b8: a cursor and a subsurface are
		 * composited OVER what is beneath them, so their alpha has to
		 * mean something.  A client using XRGB simply sets it to 0xff.
		 */
		s->img = pixman_image_create_bits(PIXMAN_a8r8g8b8, w, h,
		    s->pix, w * 4);
		if (s->img == NULL)
			return -1;
	}

	wl_shm_buffer_begin_access(shm);
	src = wl_shm_buffer_get_data(shm);
	if (src != NULL) {
		for (y = 0; y < h; y++)
			memcpy(s->pix + (size_t)y * w,
			    (const char *)src + (size_t)y * stride,
			    (size_t)w * 4);
	}
	wl_shm_buffer_end_access(shm);

	if (src == NULL) {
		wlog("commit: shm buffer has no data\n");
		return -1;
	}

	/*
	 * An XRGB buffer carries no alpha; force it opaque, or PIXMAN_OP_OVER
	 * would treat the whole surface as fully transparent and draw nothing.
	 */
	if (wl_shm_buffer_get_format(shm) == WL_SHM_FORMAT_XRGB8888) {
		int i, n = w * h;

		for (i = 0; i < n; i++)
			s->pix[i] |= 0xff000000u;
	}

	{
		uint32_t sum = 0;
		int i, n = w * h;

		for (i = 0; i < n; i++)
			sum = sum * 31u + s->pix[i];
		wlog("commit: %dx%d stride=%d checksum=0x%08x\n", w, h, stride,
		    sum);
	}

	return 0;
}

/* ------------------------------------------------------------- wl_surface */

/*
 * A minimized toplevel, or one on another desktop, is still mapped: the client
 * has no idea about either, and must not -- both are the compositor's business,
 * and Wayland has no event to tell a client about them.  So we simply stop
 * drawing it and stop letting the pointer hit it.  Everything that walks
 * C.toplevels for something the user can see or click asks this, not s->mapped.
 *
 * Layer surfaces (panels, docks, wallpapers) are on every desktop, and so are
 * not asked.
 */
static int
toplevel_visible(const struct surface *s)
{
	return s->mapped && !s->minimized && s->ws == C.ws;
}

static struct surface *
top_toplevel(void)
{
	struct surface *s, *top = NULL;

	wl_list_for_each(s, &C.toplevels, link)
		if (toplevel_visible(s))
			top = s;
	return top;
}

static void
surface_destroy(struct wl_resource *resource)
{
	struct surface *s = wl_resource_get_user_data(resource);
	struct wl_resource *cb, *tmp;
	struct surface *c, *ctmp;

	if (s == NULL)
		return;

	if (s->mapped)
		dmg_add_surface(s);	/* erase it, while we know where it is */

	wl_resource_for_each_safe(cb, tmp, &s->frame_callbacks) {
		wl_list_remove(wl_resource_get_link(cb));	/* see above */
		wl_resource_destroy(cb);
	}

	/* Orphan any children rather than leave them pointing at freed memory. */
	wl_list_for_each_safe(c, ctmp, &s->children, sibling) {
		wl_list_remove(&c->sibling);
		wl_list_init(&c->sibling);
		c->parent = NULL;
		c->mapped = 0;
	}

	if (s->parent != NULL)
		wl_list_remove(&s->sibling);
	if (s->role == ROLE_TOPLEVEL)
		wl_list_remove(&s->link);
	if (s->role == ROLE_LAYER)
		wl_list_remove(&s->llink);

	if (s->img != NULL)
		pixman_image_unref(s->img);
	free(s->pix);
	pixman_region32_fini(&s->damage);

	if (s->role == ROLE_TOPLEVEL)
		ftl_closed(s);

	if (C.focus == s) {
		C.focus = top_toplevel();
		ftl_focus_changed();
	}
	if (C.ptr_focus == s)
		C.ptr_focus = NULL;
	if (C.dragging == s)
		C.dragging = NULL;
	if (C.resizing == s)
		C.resizing = NULL;
	if (C.cursor == s)
		C.cursor = NULL;

	/*
	 * s->layer_surface still carries s as its user_data and has a destroy
	 * handler (layer_surface_resource_destroy) that dereferences it.  On
	 * wl_client teardown the wl_map is walked in an unspecified order, so that
	 * resource can be destroyed after this wl_surface -- clear the back-pointer
	 * so the handler sees NULL and bails rather than touching freed memory.
	 * Without this, closing a layer-shell client (e.g. lxqt-panel aborting when
	 * its menu popup is clicked) faults wlcompd with a NULL wl_list_remove and
	 * takes the whole session down.  The xdg_* and subsurface resources use a
	 * NULL destroy handler, so they are already safe.
	 */
	if (s->layer_surface != NULL)
		wl_resource_set_user_data(s->layer_surface, NULL);

	free(s);
}

static void
surf_destroy(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	wl_resource_destroy(r);
}

static void
surf_attach(struct wl_client *c, struct wl_resource *r,
    struct wl_resource *buffer, int32_t x, int32_t y)
{
	struct surface *s = wl_resource_get_user_data(r);

	(void)c; (void)x; (void)y;
	s->pending_buffer = buffer;
	s->pending_buffer_set = 1;
}

static void
surf_damage(struct wl_client *c, struct wl_resource *r,
    int32_t x, int32_t y, int32_t w, int32_t h)
{
	struct surface *s = wl_resource_get_user_data(r);

	(void)c;
	if (w > 0 && h > 0)
		pixman_region32_union_rect(&s->damage, &s->damage, x, y, w, h);
}

static void
surf_frame(struct wl_client *c, struct wl_resource *r, uint32_t id)
{
	struct surface *s = wl_resource_get_user_data(r);
	struct wl_resource *cb;

	cb = wl_resource_create(c, &wl_callback_interface, 1, id);
	if (cb == NULL) {
		wl_client_post_no_memory(c);
		return;
	}
	wl_list_insert(&s->frame_callbacks, wl_resource_get_link(cb));
}

static void
surf_set_region(struct wl_client *c, struct wl_resource *r,
    struct wl_resource *region)
{
	(void)c; (void)r; (void)region;
}

static void
surf_commit(struct wl_client *c, struct wl_resource *r)
{
	struct surface *s = wl_resource_get_user_data(r);
	int ow = s->w, oh = s->h;

	(void)c;

	/*
	 * A layer surface's first commit carries no buffer: it is the client
	 * saying "I have told you my anchors and my size, now tell me my
	 * geometry".  Only now are set_size/set_anchor/set_margin actually in.
	 */
	if (s->role == ROLE_LAYER && !s->lconfigured) {
		s->lconfigured = 1;
		layer_send_configure(s);
		if (!s->pending_buffer_set) {
			pixman_region32_clear(&s->damage);
			return;
		}
	}

	if (!s->pending_buffer_set) {
		pixman_region32_clear(&s->damage);
		return;
	}
	s->pending_buffer_set = 0;

	if (s->pending_buffer == NULL) {		/* attach(NULL): unmap */
		if (s->mapped) {
			dmg_add_surface(s);
			s->mapped = 0;
		}
		pixman_region32_clear(&s->damage);
		return;
	}

	if (surface_take_buffer(s, s->pending_buffer) == 0) {
		if (!s->mapped) {
			s->mapped = 1;
			if (s->role == ROLE_TOPLEVEL) {
				/*
				 * Announce it only now.  A toplevel exists as a
				 * wl_surface well before it has a buffer, and a
				 * taskbar entry for a window that is not on
				 * screen yet is just a flicker.
				 */
				ftl_announce(s);
				/*
				 * Give the freshly-mapped window keyboard focus
				 * through keyboard_focus(), not a bare
				 * "C.focus = s".  The assignment sets the focus
				 * variable but never sends wl_keyboard.enter, and
				 * a later click can't repair it: keyboard_focus()
				 * early-returns when C.focus already equals the
				 * surface.  The client would then be handed key
				 * events for a surface it was never told it holds
				 * focus on, and a Wayland client (Qt) discards
				 * those -- so the terminal could show output but
				 * never accept a keystroke.  keyboard_focus()
				 * sends the enter and also does ftl_focus_changed.
				 */
				keyboard_focus(s);
			}
			dmg_add_surface(s);
			wlog("%s mapped %dx%d \"%s\"\n",
			    s->role == ROLE_POPUP ? "popup" :
			    s->role == ROLE_SUBSURFACE ? "subsurface" :
			    s->role == ROLE_CURSOR ? "cursor" : "surface",
			    s->w, s->h, s->title);
		} else if (s->w != ow || s->h != oh) {
			/*
			 * Both extents: the new one to draw it, the old one to
			 * erase what it no longer covers -- a shrink would
			 * otherwise strand the last frame's pixels on screen.
			 */
			int ox, oy;

			surface_origin(s, &ox, &oy);
			if (s->role == ROLE_TOPLEVEL)
				dmg_add(s->x, s->y, ow, TITLEH + oh);
			else
				dmg_add(ox, oy, ow, oh);
			dmg_add_surface(s);
			wlog("surface resized %dx%d -> %dx%d\n", ow, oh,
			    s->w, s->h);
		} else if (pixman_region32_not_empty(&s->damage)) {
			/* Only what the client said it redrew, in screen space. */
			pixman_region32_t d;
			int ox, oy;

			surface_origin(s, &ox, &oy);
			pixman_region32_init(&d);
			pixman_region32_copy(&d, &s->damage);
			pixman_region32_intersect_rect(&d, &d, 0, 0, s->w, s->h);
			pixman_region32_translate(&d, ox, oy);
			pixman_region32_union(&C.damage, &C.damage, &d);
			pixman_region32_fini(&d);
		} else {
			dmg_add_surface(s);	/* it named none: assume all */
		}
	}

	pixman_region32_clear(&s->damage);

	/* Released at once: we hold our own copy, so the client may reuse it. */
	wl_buffer_send_release(s->pending_buffer);
	s->pending_buffer = NULL;
}

static void
surf_noop_i(struct wl_client *c, struct wl_resource *r, int32_t v)
{
	(void)c; (void)r; (void)v;
}

static void
surf_damage_buffer(struct wl_client *c, struct wl_resource *r,
    int32_t x, int32_t y, int32_t w, int32_t h)
{
	surf_damage(c, r, x, y, w, h);	/* no scale or transform in play */
}

static void
surf_offset(struct wl_client *c, struct wl_resource *r, int32_t x, int32_t y)
{
	(void)c; (void)r; (void)x; (void)y;
}

static const struct wl_surface_interface surface_impl = {
	.destroy		= surf_destroy,
	.attach			= surf_attach,
	.damage			= surf_damage,
	.frame			= surf_frame,
	.set_opaque_region	= surf_set_region,
	.set_input_region	= surf_set_region,
	.commit			= surf_commit,
	.set_buffer_transform	= surf_noop_i,
	.set_buffer_scale	= surf_noop_i,
	.damage_buffer		= surf_damage_buffer,
	.offset			= surf_offset,
};

/* ------------------------------------------------------------- wl_region */

static void
region_destroy(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	wl_resource_destroy(r);
}

static void
region_rect(struct wl_client *c, struct wl_resource *r,
    int32_t x, int32_t y, int32_t w, int32_t h)
{
	(void)c; (void)r; (void)x; (void)y; (void)w; (void)h;
}

static const struct wl_region_interface region_impl = {
	.destroy	= region_destroy,
	.add		= region_rect,
	.subtract	= region_rect,
};

/* ---------------------------------------------------------- wl_compositor */

static void
comp_create_surface(struct wl_client *client, struct wl_resource *r,
    uint32_t id)
{
	struct surface *s;
	static int spawn_x = 60, spawn_y = 60;

	if ((s = calloc(1, sizeof(*s))) == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_list_init(&s->frame_callbacks);
	wl_list_init(&s->ftl);
	wl_list_init(&s->children);
	wl_list_init(&s->sibling);
	wl_list_init(&s->llink);
	pixman_region32_init(&s->damage);

	s->resource = wl_resource_create(client, &wl_surface_interface,
	    wl_resource_get_version(r), id);
	if (s->resource == NULL) {
		pixman_region32_fini(&s->damage);
		free(s);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(s->resource, &surface_impl, s,
	    surface_destroy);

	/*
	 * A surface has no role yet.  It stays out of the toplevel list until
	 * it gets one -- a cursor or a subsurface never joins it at all.
	 */
	s->x = spawn_x;
	s->y = spawn_y;
	spawn_x = 60 + ((spawn_x + 30) % 240);
	spawn_y = 60 + ((spawn_y + 30) % 180);

	wlog("surface created\n");
}

static void
comp_create_region(struct wl_client *client, struct wl_resource *r,
    uint32_t id)
{
	struct wl_resource *res;

	res = wl_resource_create(client, &wl_region_interface,
	    wl_resource_get_version(r), id);
	if (res == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(res, &region_impl, NULL, NULL);
}

static const struct wl_compositor_interface compositor_impl = {
	.create_surface	= comp_create_surface,
	.create_region	= comp_create_region,
};

static void
bind_compositor(struct wl_client *client, void *data, uint32_t version,
    uint32_t id)
{
	struct wl_resource *r;

	(void)data;
	r = wl_resource_create(client, &wl_compositor_interface, version, id);
	if (r == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(r, &compositor_impl, NULL, NULL);
}

/* ------------------------------------------------------- wl_subcompositor */

static void
subsurf_destroy(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	wl_resource_destroy(r);
}

static void
subsurf_set_position(struct wl_client *c, struct wl_resource *r,
    int32_t x, int32_t y)
{
	struct surface *s = wl_resource_get_user_data(r);

	(void)c;
	if (s == NULL)
		return;

	if (s->mapped)
		dmg_add_surface(s);	/* erase it where it was */
	s->x = x;
	s->y = y;
	if (s->mapped)
		dmg_add_surface(s);	/* and draw it where it now is */
}

static void
subsurf_place(struct wl_client *c, struct wl_resource *r,
    struct wl_resource *sibling)
{
	/* Ordering among siblings; we keep creation order. */
	(void)c; (void)r; (void)sibling;
}

static void
subsurf_set_sync(struct wl_client *c, struct wl_resource *r)
{
	(void)c; (void)r;
}

static const struct wl_subsurface_interface subsurface_impl = {
	.destroy	= subsurf_destroy,
	.set_position	= subsurf_set_position,
	.place_above	= subsurf_place,
	.place_below	= subsurf_place,
	.set_sync	= subsurf_set_sync,
	.set_desync	= subsurf_set_sync,
};

static void
subcomp_destroy(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	wl_resource_destroy(r);
}

static void
subcomp_get_subsurface(struct wl_client *c, struct wl_resource *r, uint32_t id,
    struct wl_resource *surface, struct wl_resource *parent)
{
	struct surface *s = wl_resource_get_user_data(surface);
	struct surface *p = wl_resource_get_user_data(parent);

	if (s == NULL || p == NULL || s == p) {
		wl_client_post_no_memory(c);
		return;
	}

	s->subsurface = wl_resource_create(c, &wl_subsurface_interface,
	    wl_resource_get_version(r), id);
	if (s->subsurface == NULL) {
		wl_client_post_no_memory(c);
		return;
	}
	wl_resource_set_implementation(s->subsurface, &subsurface_impl, s, NULL);

	s->role = ROLE_SUBSURFACE;
	s->parent = p;
	s->x = 0;
	s->y = 0;
	wl_list_insert(p->children.prev, &s->sibling);

	wlog("subsurface created\n");
}

static const struct wl_subcompositor_interface subcompositor_impl = {
	.destroy		= subcomp_destroy,
	.get_subsurface		= subcomp_get_subsurface,
};

static void
bind_subcompositor(struct wl_client *client, void *data, uint32_t version,
    uint32_t id)
{
	struct wl_resource *r;

	(void)data;
	r = wl_resource_create(client, &wl_subcompositor_interface, version, id);
	if (r == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(r, &subcompositor_impl, NULL, NULL);
}

/* ------------------------------------------------------------- xdg_shell */

static void
xdgtop_destroy(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	wl_resource_destroy(r);
}

static void
xdgtop_set_title(struct wl_client *c, struct wl_resource *r, const char *title)
{
	struct surface *s = wl_resource_get_user_data(r);

	(void)c;
	if (s != NULL && title != NULL) {
		strlcpy(s->title, title, sizeof(s->title));
		dmg_add(s->x, s->y, s->w, TITLEH);
		ftl_title_changed(s);
	}
}

static void
xdgtop_set_app_id(struct wl_client *c, struct wl_resource *r, const char *id)
{
	struct surface *s = wl_resource_get_user_data(r);

	(void)c;
	/*
	 * We have no use for the app id ourselves, but a taskbar does: it is how
	 * it finds the .desktop file, and so the icon.
	 */
	if (s != NULL && id != NULL) {
		strlcpy(s->app_id, id, sizeof(s->app_id));
		ftl_app_id_changed(s);
	}
}

static void
xdgtop_move(struct wl_client *c, struct wl_resource *r,
    struct wl_resource *seat, uint32_t serial)
{
	struct surface *s = wl_resource_get_user_data(r);

	(void)c; (void)seat; (void)serial;
	if (s != NULL) {
		C.dragging = s;
		C.drag_dx = C.cx - s->x;
		C.drag_dy = C.cy - s->y;
	}
}

static void
xdgtop_resize(struct wl_client *c, struct wl_resource *r,
    struct wl_resource *seat, uint32_t serial, uint32_t edges)
{
	struct surface *s = wl_resource_get_user_data(r);

	(void)c; (void)seat; (void)serial; (void)edges;
	if (s == NULL || !s->mapped)
		return;

	C.resizing = s;
	C.rz_cx0 = C.cx;
	C.rz_cy0 = C.cy;
	C.rz_w0 = s->w;
	C.rz_h0 = s->h;
	wlog("client asked to resize from %dx%d\n", s->w, s->h);
}

static void
xdgtop_noop(struct wl_client *c, struct wl_resource *r)
{
	(void)c; (void)r;
}

static void
xdgtop_set_size(struct wl_client *c, struct wl_resource *r,
    int32_t w, int32_t h)
{
	(void)c; (void)r; (void)w; (void)h;
}

static void
xdgtop_set_parent(struct wl_client *c, struct wl_resource *r,
    struct wl_resource *parent)
{
	(void)c; (void)r; (void)parent;
}

static void
xdgtop_show_window_menu(struct wl_client *c, struct wl_resource *r,
    struct wl_resource *seat, uint32_t serial, int32_t x, int32_t y)
{
	(void)c; (void)r; (void)seat; (void)serial; (void)x; (void)y;
}

static void
xdgtop_set_fullscreen(struct wl_client *c, struct wl_resource *r,
    struct wl_resource *output)
{
	(void)c; (void)r; (void)output;
}

static const struct xdg_toplevel_interface xdg_toplevel_impl = {
	.destroy		= xdgtop_destroy,
	.set_parent		= xdgtop_set_parent,
	.set_title		= xdgtop_set_title,
	.set_app_id		= xdgtop_set_app_id,
	.show_window_menu	= xdgtop_show_window_menu,
	.move			= xdgtop_move,
	.resize			= xdgtop_resize,
	.set_max_size		= xdgtop_set_size,
	.set_min_size		= xdgtop_set_size,
	.set_maximized		= xdgtop_noop,
	.unset_maximized	= xdgtop_noop,
	.set_fullscreen		= xdgtop_set_fullscreen,
	.unset_fullscreen	= xdgtop_noop,
	.set_minimized		= xdgtop_noop,
};

/* ---- xdg_positioner: where a popup goes ------------------------------- */

static void
pos_destroy(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	wl_resource_destroy(r);
}

static void
pos_set_size(struct wl_client *c, struct wl_resource *r, int32_t w, int32_t h)
{
	struct positioner *p = wl_resource_get_user_data(r);

	(void)c;
	p->w = w;
	p->h = h;
}

static void
pos_set_anchor_rect(struct wl_client *c, struct wl_resource *r,
    int32_t x, int32_t y, int32_t w, int32_t h)
{
	struct positioner *p = wl_resource_get_user_data(r);

	(void)c;
	p->ax = x;
	p->ay = y;
	p->aw = w;
	p->ah = h;
}

static void
pos_set_anchor(struct wl_client *c, struct wl_resource *r, uint32_t anchor)
{
	struct positioner *p = wl_resource_get_user_data(r);

	(void)c;
	p->anchor = anchor;
}

static void
pos_set_gravity(struct wl_client *c, struct wl_resource *r, uint32_t gravity)
{
	struct positioner *p = wl_resource_get_user_data(r);

	(void)c;
	p->gravity = gravity;
}

static void
pos_set_constraint_adjustment(struct wl_client *c, struct wl_resource *r,
    uint32_t adj)
{
	(void)c; (void)r; (void)adj;
}

static void
pos_set_offset(struct wl_client *c, struct wl_resource *r, int32_t x, int32_t y)
{
	struct positioner *p = wl_resource_get_user_data(r);

	(void)c;
	p->ox = x;
	p->oy = y;
}

static void
pos_noop(struct wl_client *c, struct wl_resource *r)
{
	(void)c; (void)r;
}

static void
pos_set_parent_size(struct wl_client *c, struct wl_resource *r,
    int32_t w, int32_t h)
{
	(void)c; (void)r; (void)w; (void)h;
}

static void
pos_set_parent_configure(struct wl_client *c, struct wl_resource *r,
    uint32_t serial)
{
	(void)c; (void)r; (void)serial;
}

static const struct xdg_positioner_interface positioner_impl = {
	.destroy			= pos_destroy,
	.set_size			= pos_set_size,
	.set_anchor_rect		= pos_set_anchor_rect,
	.set_anchor			= pos_set_anchor,
	.set_gravity			= pos_set_gravity,
	.set_constraint_adjustment	= pos_set_constraint_adjustment,
	.set_offset			= pos_set_offset,
	.set_reactive			= pos_noop,
	.set_parent_size		= pos_set_parent_size,
	.set_parent_configure		= pos_set_parent_configure,
};

static void
positioner_resource_destroy(struct wl_resource *r)
{
	free(wl_resource_get_user_data(r));
}

/*
 * Resolve a positioner into an offset from the parent's content origin.
 *
 * The anchor picks a point on the anchor rectangle; the gravity says which way
 * the popup hangs from it.  This is the geometry a menu depends on -- get the
 * gravity backwards and every menu opens over the item that spawned it.
 */
static void
positioner_resolve(const struct positioner *p, int *px, int *py)
{
	int x = p->ax, y = p->ay;

	/* Anchor point on the anchor rectangle. */
	if (p->anchor == XDG_POSITIONER_ANCHOR_TOP ||
	    p->anchor == XDG_POSITIONER_ANCHOR_BOTTOM ||
	    p->anchor == XDG_POSITIONER_ANCHOR_NONE)
		x += p->aw / 2;
	else if (p->anchor == XDG_POSITIONER_ANCHOR_RIGHT ||
	    p->anchor == XDG_POSITIONER_ANCHOR_TOP_RIGHT ||
	    p->anchor == XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT)
		x += p->aw;

	if (p->anchor == XDG_POSITIONER_ANCHOR_LEFT ||
	    p->anchor == XDG_POSITIONER_ANCHOR_RIGHT ||
	    p->anchor == XDG_POSITIONER_ANCHOR_NONE)
		y += p->ah / 2;
	else if (p->anchor == XDG_POSITIONER_ANCHOR_BOTTOM ||
	    p->anchor == XDG_POSITIONER_ANCHOR_BOTTOM_LEFT ||
	    p->anchor == XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT)
		y += p->ah;

	/* Gravity: which side of that point the popup occupies. */
	if (p->gravity == XDG_POSITIONER_GRAVITY_LEFT ||
	    p->gravity == XDG_POSITIONER_GRAVITY_TOP_LEFT ||
	    p->gravity == XDG_POSITIONER_GRAVITY_BOTTOM_LEFT)
		x -= p->w;
	else if (p->gravity == XDG_POSITIONER_GRAVITY_NONE ||
	    p->gravity == XDG_POSITIONER_GRAVITY_TOP ||
	    p->gravity == XDG_POSITIONER_GRAVITY_BOTTOM)
		x -= p->w / 2;

	if (p->gravity == XDG_POSITIONER_GRAVITY_TOP ||
	    p->gravity == XDG_POSITIONER_GRAVITY_TOP_LEFT ||
	    p->gravity == XDG_POSITIONER_GRAVITY_TOP_RIGHT)
		y -= p->h;
	else if (p->gravity == XDG_POSITIONER_GRAVITY_NONE ||
	    p->gravity == XDG_POSITIONER_GRAVITY_LEFT ||
	    p->gravity == XDG_POSITIONER_GRAVITY_RIGHT)
		y -= p->h / 2;

	*px = x + p->ox;
	*py = y + p->oy;
}

/* ---- xdg_popup -------------------------------------------------------- */

static void
popup_destroy(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	wl_resource_destroy(r);
}

static void
popup_grab(struct wl_client *c, struct wl_resource *r,
    struct wl_resource *seat, uint32_t serial)
{
	/* We have no explicit grab; a click outside dismisses the popup, which
	 * is handled where the button is dispatched. */
	(void)c; (void)r; (void)seat; (void)serial;
}

static void
popup_reposition(struct wl_client *c, struct wl_resource *r,
    struct wl_resource *positioner, uint32_t token)
{
	struct surface *s = wl_resource_get_user_data(r);
	struct positioner *p = wl_resource_get_user_data(positioner);

	(void)c;
	if (s == NULL || p == NULL)
		return;

	if (s->mapped)
		dmg_add_surface(s);
	positioner_resolve(p, &s->x, &s->y);
	if (s->mapped)
		dmg_add_surface(s);

	xdg_popup_send_repositioned(s->xdg_popup, token);
}

static const struct xdg_popup_interface popup_impl = {
	.destroy	= popup_destroy,
	.grab		= popup_grab,
	.reposition	= popup_reposition,
};

/* Dismiss a popup (and any popup of its own) -- a click outside it, say. */
static void
popup_dismiss(struct surface *s)
{
	struct surface *c, *tmp;

	wl_list_for_each_safe(c, tmp, &s->children, sibling)
		if (c->role == ROLE_POPUP)
			popup_dismiss(c);

	if (s->xdg_popup != NULL) {
		if (s->mapped)
			dmg_add_surface(s);
		xdg_popup_send_popup_done(s->xdg_popup);
	}
}

static void
dismiss_popups_of(struct surface *s)
{
	struct surface *c, *tmp;

	wl_list_for_each_safe(c, tmp, &s->children, sibling)
		if (c->role == ROLE_POPUP)
			popup_dismiss(c);
}

/* ---- xdg_surface ------------------------------------------------------ */

static void
xdgsurf_destroy(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	wl_resource_destroy(r);
}

static void
xdgsurf_get_toplevel(struct wl_client *c, struct wl_resource *r, uint32_t id)
{
	struct surface *s = wl_resource_get_user_data(r);

	s->xdg_toplevel = wl_resource_create(c, &xdg_toplevel_interface,
	    wl_resource_get_version(r), id);
	if (s->xdg_toplevel == NULL) {
		wl_client_post_no_memory(c);
		return;
	}
	wl_resource_set_implementation(s->xdg_toplevel, &xdg_toplevel_impl, s,
	    NULL);

	s->role = ROLE_TOPLEVEL;
	s->ws = C.ws;			/* it opens on the desktop you are on */
	wl_list_insert(C.toplevels.prev, &s->link);	/* on top */

	/* A zero size means "you choose", which is what a first configure says.
	 * A client must not draw until it has seen and ack'ed this. */
	send_configure(s, 0, 0, 0);

	wlog("xdg_toplevel created\n");
}

static void
xdgsurf_get_popup(struct wl_client *c, struct wl_resource *r, uint32_t id,
    struct wl_resource *parent, struct wl_resource *positioner)
{
	struct surface *s = wl_resource_get_user_data(r);
	struct positioner *p = wl_resource_get_user_data(positioner);
	struct surface *ps;

	if (parent == NULL || p == NULL) {
		wl_client_post_no_memory(c);
		return;
	}
	/* The parent is an xdg_surface; its user data is the surface. */
	ps = wl_resource_get_user_data(parent);
	if (ps == NULL) {
		wl_client_post_no_memory(c);
		return;
	}

	s->xdg_popup = wl_resource_create(c, &xdg_popup_interface,
	    wl_resource_get_version(r), id);
	if (s->xdg_popup == NULL) {
		wl_client_post_no_memory(c);
		return;
	}
	wl_resource_set_implementation(s->xdg_popup, &popup_impl, s, NULL);

	s->role = ROLE_POPUP;
	s->parent = ps;
	wl_list_insert(ps->children.prev, &s->sibling);

	positioner_resolve(p, &s->x, &s->y);

	/* Tell it where and how big it is, then configure the xdg_surface. */
	xdg_popup_send_configure(s->xdg_popup, s->x, s->y,
	    p->w > 0 ? p->w : 1, p->h > 0 ? p->h : 1);
	xdg_surface_send_configure(s->xdg_surface,
	    wl_display_next_serial(C.display));

	wlog("xdg_popup created at %+d%+d %dx%d\n", s->x, s->y, p->w, p->h);
}

static void
xdgsurf_set_window_geometry(struct wl_client *c, struct wl_resource *r,
    int32_t x, int32_t y, int32_t w, int32_t h)
{
	(void)c; (void)r; (void)x; (void)y; (void)w; (void)h;
}

static void
xdgsurf_ack_configure(struct wl_client *c, struct wl_resource *r,
    uint32_t serial)
{
	(void)c; (void)r; (void)serial;
}

static const struct xdg_surface_interface xdg_surface_impl = {
	.destroy		= xdgsurf_destroy,
	.get_toplevel		= xdgsurf_get_toplevel,
	.get_popup		= xdgsurf_get_popup,
	.set_window_geometry	= xdgsurf_set_window_geometry,
	.ack_configure		= xdgsurf_ack_configure,
};

static void
wmbase_destroy(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	wl_resource_destroy(r);
}

static void
wmbase_create_positioner(struct wl_client *c, struct wl_resource *r,
    uint32_t id)
{
	struct wl_resource *res;
	struct positioner *p;

	if ((p = calloc(1, sizeof(*p))) == NULL) {
		wl_client_post_no_memory(c);
		return;
	}

	res = wl_resource_create(c, &xdg_positioner_interface,
	    wl_resource_get_version(r), id);
	if (res == NULL) {
		free(p);
		wl_client_post_no_memory(c);
		return;
	}
	wl_resource_set_implementation(res, &positioner_impl, p,
	    positioner_resource_destroy);
}

static void
wmbase_get_xdg_surface(struct wl_client *c, struct wl_resource *r, uint32_t id,
    struct wl_resource *surface)
{
	struct surface *s = wl_resource_get_user_data(surface);

	s->xdg_surface = wl_resource_create(c, &xdg_surface_interface,
	    wl_resource_get_version(r), id);
	if (s->xdg_surface == NULL) {
		wl_client_post_no_memory(c);
		return;
	}
	wl_resource_set_implementation(s->xdg_surface, &xdg_surface_impl, s,
	    NULL);
}

static void
wmbase_pong(struct wl_client *c, struct wl_resource *r, uint32_t serial)
{
	(void)c; (void)r; (void)serial;
}

static const struct xdg_wm_base_interface wm_base_impl = {
	.destroy		= wmbase_destroy,
	.create_positioner	= wmbase_create_positioner,
	.get_xdg_surface	= wmbase_get_xdg_surface,
	.pong			= wmbase_pong,
};

static void
bind_wm_base(struct wl_client *client, void *data, uint32_t version,
    uint32_t id)
{
	struct wl_resource *r;

	(void)data;
	r = wl_resource_create(client, &xdg_wm_base_interface, version, id);
	if (r == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(r, &wm_base_impl, NULL, NULL);
}


/* ------------------------------------------------------- wlr-layer-shell */

/*
 * Where a panel, dock or wallpaper goes.
 *
 * The client names the edges it is anchored to and (optionally) a size; the
 * compositor works out the rest.  Anchoring to two opposite edges means "span
 * that axis", which is how a panel comes out the full width of the screen
 * without having to know how wide the screen is.  A size of zero on an axis is
 * only legal when the surface is anchored to both of its edges -- otherwise
 * there is nothing to derive it from, and the protocol says to raise an error.
 *
 * The exclusive zone is the strip a panel reserves for itself so that other
 * windows do not sit underneath it.  We record it and report the usable area,
 * but nothing is forced into it yet: wlcompd's toplevels float, so there is no
 * maximised geometry for it to shrink.  It becomes load-bearing the moment
 * maximise or tiling arrives.
 */
static void
layer_recompute(struct surface *s)
{
	int sw = fbgui_width(C.fb), sh = fbgui_height(C.fb);
	int mt = s->lmargin[0], mr = s->lmargin[1];
	int mb = s->lmargin[2], ml = s->lmargin[3];
	uint32_t a = s->lanchor;
	int anchored_lr, anchored_tb;
	int w, h, x, y;

	anchored_lr = (a & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT) &&
	    (a & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	anchored_tb = (a & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) &&
	    (a & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM);

	/* Width: spanned if anchored both sides, else what the client asked. */
	w = s->lw;
	if (w <= 0)
		w = anchored_lr ? (sw - ml - mr) : sw;
	h = s->lh;
	if (h <= 0)
		h = anchored_tb ? (sh - mt - mb) : sh;

	if (w < 1) w = 1;
	if (h < 1) h = 1;

	/* Horizontal placement. */
	if (anchored_lr || !(a & (ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
	    ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)))
		x = anchored_lr ? ml : (sw - w) / 2;	/* spanned, or centred */
	else if (a & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)
		x = ml;
	else
		x = sw - w - mr;

	/* Vertical placement. */
	if (anchored_tb || !(a & (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
	    ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)))
		y = anchored_tb ? mt : (sh - h) / 2;
	else if (a & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)
		y = mt;
	else
		y = sh - h - mb;

	if (s->mapped)
		dmg_add_surface(s);	/* erase it where it was */
	s->x = x;
	s->y = y;
	s->cfg_w = w;
	s->cfg_h = h;
	if (s->mapped)
		dmg_add_surface(s);
}

static void
layer_send_configure(struct surface *s)
{
	layer_recompute(s);
	zwlr_layer_surface_v1_send_configure(s->layer_surface,
	    wl_display_next_serial(C.display), s->cfg_w, s->cfg_h);
}

/* The area left over once every panel has taken its exclusive strip. */
static void
layer_usable_area(int *x, int *y, int *w, int *h)
{
	struct surface *s;
	int i;

	*x = 0;
	*y = 0;
	*w = fbgui_width(C.fb);
	*h = fbgui_height(C.fb);

	for (i = 0; i < NLAYERS; i++) {
		wl_list_for_each(s, &C.layers[i], llink) {
			int e = s->lexclusive;

			if (!s->mapped || e <= 0)
				continue;

			if (s->lanchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) {
				*y += e;
				*h -= e;
			} else if (s->lanchor &
			    ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM) {
				*h -= e;
			} else if (s->lanchor &
			    ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT) {
				*x += e;
				*w -= e;
			} else if (s->lanchor &
			    ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT) {
				*w -= e;
			}
		}
	}
}

static void
lsurf_set_size(struct wl_client *c, struct wl_resource *r,
    uint32_t w, uint32_t h)
{
	struct surface *s = wl_resource_get_user_data(r);

	(void)c;
	s->lw = (int32_t)w;
	s->lh = (int32_t)h;
}

static void
lsurf_set_anchor(struct wl_client *c, struct wl_resource *r, uint32_t anchor)
{
	struct surface *s = wl_resource_get_user_data(r);

	(void)c;
	s->lanchor = anchor;
}

static void
lsurf_set_exclusive_zone(struct wl_client *c, struct wl_resource *r,
    int32_t zone)
{
	struct surface *s = wl_resource_get_user_data(r);

	(void)c;
	s->lexclusive = zone;
}

static void
lsurf_set_margin(struct wl_client *c, struct wl_resource *r,
    int32_t top, int32_t right, int32_t bottom, int32_t left)
{
	struct surface *s = wl_resource_get_user_data(r);

	(void)c;
	s->lmargin[0] = top;
	s->lmargin[1] = right;
	s->lmargin[2] = bottom;
	s->lmargin[3] = left;
}

static void
lsurf_set_keyboard_interactivity(struct wl_client *c, struct wl_resource *r,
    uint32_t ki)
{
	struct surface *s = wl_resource_get_user_data(r);

	(void)c;
	s->lkbd = ki;
}

static void
lsurf_get_popup(struct wl_client *c, struct wl_resource *r,
    struct wl_resource *popup)
{
	/* A menu opened from a panel.  The popup already exists as an
	 * xdg_popup; re-parent it onto the layer surface so it is positioned
	 * and stacked with it. */
	struct surface *s = wl_resource_get_user_data(r);
	struct surface *p = wl_resource_get_user_data(popup);

	(void)c;
	if (s == NULL || p == NULL)
		return;

	if (p->parent != NULL)
		wl_list_remove(&p->sibling);
	p->parent = s;
	wl_list_insert(s->children.prev, &p->sibling);
}

static void
lsurf_ack_configure(struct wl_client *c, struct wl_resource *r, uint32_t serial)
{
	(void)c; (void)r; (void)serial;
}

static void
lsurf_destroy(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	wl_resource_destroy(r);
}

static void
lsurf_set_layer(struct wl_client *c, struct wl_resource *r, uint32_t layer)
{
	struct surface *s = wl_resource_get_user_data(r);

	(void)c;
	if (s == NULL || layer >= NLAYERS)
		return;

	wl_list_remove(&s->llink);
	s->layer = layer;
	wl_list_insert(C.layers[layer].prev, &s->llink);
	dmg_add_surface(s);
}

static void
lsurf_set_exclusive_edge(struct wl_client *c, struct wl_resource *r,
    uint32_t edge)
{
	(void)c; (void)r; (void)edge;
}

static const struct zwlr_layer_surface_v1_interface layer_surface_impl = {
	.set_size			= lsurf_set_size,
	.set_anchor			= lsurf_set_anchor,
	.set_exclusive_zone		= lsurf_set_exclusive_zone,
	.set_margin			= lsurf_set_margin,
	.set_keyboard_interactivity	= lsurf_set_keyboard_interactivity,
	.get_popup			= lsurf_get_popup,
	.ack_configure			= lsurf_ack_configure,
	.destroy			= lsurf_destroy,
	.set_layer			= lsurf_set_layer,
	.set_exclusive_edge		= lsurf_set_exclusive_edge,
};

static void
layer_surface_resource_destroy(struct wl_resource *r)
{
	struct surface *s = wl_resource_get_user_data(r);

	if (s == NULL)
		return;
	if (s->mapped)
		dmg_add_surface(s);
	wl_list_remove(&s->llink);
	wl_list_init(&s->llink);
	s->layer_surface = NULL;
	s->role = ROLE_NONE;
	s->mapped = 0;
}

static void
lshell_get_layer_surface(struct wl_client *c, struct wl_resource *r,
    uint32_t id, struct wl_resource *surface, struct wl_resource *output,
    uint32_t layer, const char *namespace)
{
	struct surface *s = wl_resource_get_user_data(surface);

	(void)output;

	if (s == NULL) {
		wl_client_post_no_memory(c);
		return;
	}
	if (layer >= NLAYERS) {
		wl_resource_post_error(r,
		    ZWLR_LAYER_SHELL_V1_ERROR_INVALID_LAYER,
		    "invalid layer %u", layer);
		return;
	}

	s->layer_surface = wl_resource_create(c,
	    &zwlr_layer_surface_v1_interface, wl_resource_get_version(r), id);
	if (s->layer_surface == NULL) {
		wl_client_post_no_memory(c);
		return;
	}
	wl_resource_set_implementation(s->layer_surface, &layer_surface_impl, s,
	    layer_surface_resource_destroy);

	s->role = ROLE_LAYER;
	s->layer = layer;
	wl_list_insert(C.layers[layer].prev, &s->llink);

	if (namespace != NULL)
		strlcpy(s->title, namespace, sizeof(s->title));

	/*
	 * NOT configured here.  At this point the client has only asked for the
	 * layer surface: set_size, set_anchor and the margins are still on their
	 * way, so anything computed now would be derived from zeroes -- which is
	 * exactly how a panel ends up full-screen instead of a strip along an
	 * edge.  The protocol has the client set its properties and then commit;
	 * the configure is the answer to that commit.  See surf_commit().
	 */
	wlog("layer surface \"%s\" on layer %u\n", s->title, layer);
}

static void
lshell_destroy(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	wl_resource_destroy(r);
}

static const struct zwlr_layer_shell_v1_interface layer_shell_impl = {
	.get_layer_surface	= lshell_get_layer_surface,
	.destroy		= lshell_destroy,
};

static void
bind_layer_shell(struct wl_client *client, void *data, uint32_t version,
    uint32_t id)
{
	struct wl_resource *r;

	(void)data;
	r = wl_resource_create(client, &zwlr_layer_shell_v1_interface, version,
	    id);
	if (r == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(r, &layer_shell_impl, NULL, NULL);
}

/* -------------------------------------------------- xdg-decoration -------- */
/*
 * wlcompd always draws server-side decorations (the title bar with a close box;
 * see render_surface()).  Advertise xdg-decoration and answer every request with
 * SERVER_SIDE: without this a Qt/GTK client falls back to drawing its OWN
 * client-side title bar on top of ours, so every window shows two title bars.
 * We keep no per-decoration state -- the answer is always the same.
 */
static void
tdeco_destroy(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	wl_resource_destroy(r);
}

static void
tdeco_set_mode(struct wl_client *c, struct wl_resource *r, uint32_t mode)
{
	(void)c; (void)mode;
	zxdg_toplevel_decoration_v1_send_configure(r,
	    ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static void
tdeco_unset_mode(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	zxdg_toplevel_decoration_v1_send_configure(r,
	    ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static const struct zxdg_toplevel_decoration_v1_interface tdeco_impl = {
	.destroy	= tdeco_destroy,
	.set_mode	= tdeco_set_mode,
	.unset_mode	= tdeco_unset_mode,
};

static void
decomgr_destroy(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	wl_resource_destroy(r);
}

static void
decomgr_get_toplevel_decoration(struct wl_client *c, struct wl_resource *r,
    uint32_t id, struct wl_resource *toplevel)
{
	struct wl_resource *d;

	(void)toplevel;
	d = wl_resource_create(c, &zxdg_toplevel_decoration_v1_interface,
	    wl_resource_get_version(r), id);
	if (d == NULL) {
		wl_client_post_no_memory(c);
		return;
	}
	wl_resource_set_implementation(d, &tdeco_impl, NULL, NULL);
	/* The initial configure is also our final answer: server-side. */
	zxdg_toplevel_decoration_v1_send_configure(d,
	    ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static const struct zxdg_decoration_manager_v1_interface decomgr_impl = {
	.destroy		 = decomgr_destroy,
	.get_toplevel_decoration = decomgr_get_toplevel_decoration,
};

static void
bind_deco_manager(struct wl_client *client, void *data, uint32_t version,
    uint32_t id)
{
	struct wl_resource *r;

	(void)data;
	r = wl_resource_create(client, &zxdg_decoration_manager_v1_interface,
	    version, id);
	if (r == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(r, &decomgr_impl, NULL, NULL);
}

/* -------------------------------------------------- wl_data_device (clip) */


static void
dsrc_offer(struct wl_client *c, struct wl_resource *r, const char *mime)
{
	(void)c; (void)r; (void)mime;
}

static void
dsrc_destroy(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	wl_resource_destroy(r);
}

static void
dsrc_set_actions(struct wl_client *c, struct wl_resource *r, uint32_t actions)
{
	(void)c; (void)r; (void)actions;
}

static const struct wl_data_source_interface data_source_impl = {
	.offer		= dsrc_offer,
	.destroy	= dsrc_destroy,
	.set_actions	= dsrc_set_actions,
};

static void
ddev_start_drag(struct wl_client *c, struct wl_resource *r,
    struct wl_resource *source, struct wl_resource *origin,
    struct wl_resource *icon, uint32_t serial)
{
	/* Drag and drop is not implemented; accepting the request and doing
	 * nothing is better than a protocol error, which would kill the app. */
	(void)c; (void)r; (void)source; (void)origin; (void)icon; (void)serial;
}

static void
ddev_set_selection(struct wl_client *c, struct wl_resource *r,
    struct wl_resource *source, uint32_t serial)
{
	(void)c; (void)r; (void)serial;

	/*
	 * Remember who owns the clipboard.  Handing the offer on to other
	 * clients (wl_data_device.data_offer + .selection) is the other half and
	 * is not done yet -- so a copy is recorded, but a paste in another
	 * client will not see it.
	 */
	C.selection = source;
	wlog("selection set (clipboard owner recorded)\n");
}

static void
ddev_release(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	wl_resource_destroy(r);
}

static const struct wl_data_device_interface data_device_impl = {
	.start_drag	= ddev_start_drag,
	.set_selection	= ddev_set_selection,
	.release	= ddev_release,
};

static void
ddm_create_data_source(struct wl_client *c, struct wl_resource *r, uint32_t id)
{
	struct wl_resource *res;

	res = wl_resource_create(c, &wl_data_source_interface,
	    wl_resource_get_version(r), id);
	if (res == NULL) {
		wl_client_post_no_memory(c);
		return;
	}
	wl_resource_set_implementation(res, &data_source_impl, NULL, NULL);
}

static void
seat_resource_destroy(struct wl_resource *r)
{
	wl_list_remove(wl_resource_get_link(r));
}

static void
ddm_get_data_device(struct wl_client *c, struct wl_resource *r, uint32_t id,
    struct wl_resource *seat)
{
	struct wl_resource *res;

	(void)seat;
	res = wl_resource_create(c, &wl_data_device_interface,
	    wl_resource_get_version(r), id);
	if (res == NULL) {
		wl_client_post_no_memory(c);
		return;
	}
	wl_resource_set_implementation(res, &data_device_impl, NULL,
	    seat_resource_destroy);
	wl_list_insert(&C.data_devices, wl_resource_get_link(res));
}

static const struct wl_data_device_manager_interface data_device_manager_impl = {
	.create_data_source	= ddm_create_data_source,
	.get_data_device	= ddm_get_data_device,
};

static void
bind_ddm(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *r;

	(void)data;
	r = wl_resource_create(client, &wl_data_device_manager_interface,
	    version, id);
	if (r == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(r, &data_device_manager_impl, NULL, NULL);
}

/* ---------------------------------------------------------------- wl_seat */

/*
 * A client setting its own cursor.  The surface it hands us takes the cursor
 * role: we draw it at the pointer instead of our own block, offset by the
 * hotspot the client names.
 */
static void
pointer_set_cursor(struct wl_client *c, struct wl_resource *r, uint32_t serial,
    struct wl_resource *surface, int32_t hx, int32_t hy)
{
	struct surface *s;

	(void)c; (void)r; (void)serial;

	dmg_add_cursor(C.cx, C.cy);		/* erase whatever is there now */

	if (surface == NULL) {			/* hide the cursor */
		C.cursor = NULL;
		dmg_add_cursor(C.cx, C.cy);
		return;
	}

	s = wl_resource_get_user_data(surface);
	if (s == NULL)
		return;

	s->role = ROLE_CURSOR;
	s->hx = hx;
	s->hy = hy;
	C.cursor = s;
	dmg_add_cursor(C.cx, C.cy);
}

static void
pointer_release(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	wl_resource_destroy(r);
}

static const struct wl_pointer_interface pointer_impl = {
	.set_cursor	= pointer_set_cursor,
	.release	= pointer_release,
};

static void
keyboard_release(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	wl_resource_destroy(r);
}

static const struct wl_keyboard_interface keyboard_impl = {
	.release	= keyboard_release,
};

static void
seat_get_pointer(struct wl_client *c, struct wl_resource *r, uint32_t id)
{
	struct wl_resource *res;

	res = wl_resource_create(c, &wl_pointer_interface,
	    wl_resource_get_version(r), id);
	if (res == NULL) {
		wl_client_post_no_memory(c);
		return;
	}
	wl_resource_set_implementation(res, &pointer_impl, NULL,
	    seat_resource_destroy);
	wl_list_insert(&C.pointers, wl_resource_get_link(res));
}

static void
seat_get_keyboard(struct wl_client *c, struct wl_resource *r, uint32_t id)
{
	struct wl_resource *res;

	res = wl_resource_create(c, &wl_keyboard_interface,
	    wl_resource_get_version(r), id);
	if (res == NULL) {
		wl_client_post_no_memory(c);
		return;
	}
	wl_resource_set_implementation(res, &keyboard_impl, NULL,
	    seat_resource_destroy);
	wl_list_insert(&C.keyboards, wl_resource_get_link(res));

	/* The keymap goes over as a file descriptor the client mmap()s and
	 * feeds to xkb_keymap_new_from_string() -- the path xkbprobe exercises,
	 * and the reason xkeyboard-config need not exist on this machine. */
	if (C.keymap_fd >= 0)
		wl_keyboard_send_keymap(res, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
		    C.keymap_fd, (uint32_t)C.keymap_size);
	else
		wl_keyboard_send_keymap(res,
		    WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP, -1, 0);

	if (wl_resource_get_version(res) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION)
		wl_keyboard_send_repeat_info(res, 25, 400);
}

static void
seat_get_touch(struct wl_client *c, struct wl_resource *r, uint32_t id)
{
	struct wl_resource *res;

	res = wl_resource_create(c, &wl_touch_interface,
	    wl_resource_get_version(r), id);
	if (res == NULL) {
		wl_client_post_no_memory(c);
		return;
	}
	wl_resource_set_implementation(res, NULL, NULL, NULL);
}

static void
seat_release(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	wl_resource_destroy(r);
}

static const struct wl_seat_interface seat_impl = {
	.get_pointer	= seat_get_pointer,
	.get_keyboard	= seat_get_keyboard,
	.get_touch	= seat_get_touch,
	.release	= seat_release,
};

static void
bind_seat(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *r;

	(void)data;
	r = wl_resource_create(client, &wl_seat_interface, version, id);
	if (r == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(r, &seat_impl, NULL, NULL);

	/*
	 * Pointer and keyboard only.  There is no wl_seat touch capability
	 * because there is no touch device, and no wl_pointer.axis because
	 * MINIX's input server reports no wheel: <minix/input.h> defines only
	 * INPUT_GD_X and INPUT_GD_Y on the General Desktop page.  Scrolling
	 * needs the PS/2 driver to grow wheel events first.
	 */
	wl_seat_send_capabilities(r,
	    WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
	if (version >= WL_SEAT_NAME_SINCE_VERSION)
		wl_seat_send_name(r, "minix-seat0");
}

/* -------------------------------------------------------------- wl_output */

static void
output_release(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	wl_resource_destroy(r);
}

static const struct wl_output_interface output_impl = {
	.release	= output_release,
};

static void
output_res_destroy(struct wl_resource *r)
{
	wl_list_remove(wl_resource_get_link(r));
}

static void
bind_output(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *r;
	int w = fbgui_width(C.fb), h = fbgui_height(C.fb);

	(void)data;
	r = wl_resource_create(client, &wl_output_interface, version, id);
	if (r == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	/*
	 * Keep the output resources: zwlr_foreign_toplevel_handle_v1.output_enter
	 * has to name the wl_output *that client* bound, not ours.
	 */
	wl_resource_set_implementation(r, &output_impl, NULL,
	    output_res_destroy);
	wl_list_insert(&C.outputs, wl_resource_get_link(r));

	wl_output_send_geometry(r, 0, 0, w, h, WL_OUTPUT_SUBPIXEL_UNKNOWN,
	    "MINIX", "fb0", WL_OUTPUT_TRANSFORM_NORMAL);
	wl_output_send_mode(r,
	    WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED, w, h, 60000);
	if (version >= WL_OUTPUT_SCALE_SINCE_VERSION)
		wl_output_send_scale(r, 1);
	if (version >= WL_OUTPUT_DONE_SINCE_VERSION)
		wl_output_send_done(r);
}

/* ------------------------------------ wlr-foreign-toplevel-management-v1 */

/*
 * What a taskbar needs, and the only way it can get it.
 *
 * Wayland deliberately does not let a client see, name, raise or close another
 * client's windows -- that is a security property, not an oversight.  This
 * protocol is the explicit opt-in that hands those powers to a privileged
 * client, which is what a panel is.  lxqt-panel's "wlroots" backend speaks it;
 * without it, its taskbar has nothing to show and it falls back to a dummy
 * backend.
 *
 * One handle exists per (manager, toplevel) pair: every manager that binds gets
 * told about every toplevel, and each gets its own handle resource for it.
 */

struct ftl {
	struct wl_resource	*resource;
	struct surface		*s;	/* NULL once the toplevel has gone */
	struct wl_list		 link;	/* in s->ftl */
};

static void keyboard_focus(struct surface *);	/* defined with the seat */

static struct wl_resource *
output_of(struct wl_client *client)
{
	struct wl_resource *o;

	wl_resource_for_each(o, &C.outputs)
		if (wl_resource_get_client(o) == client)
			return o;
	return NULL;
}

static void
ftl_send_state(struct ftl *f)
{
	struct wl_array a;
	uint32_t *st;

	wl_array_init(&a);
	if (f->s->maximized &&
	    (st = wl_array_add(&a, sizeof(*st))) != NULL)
		*st = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED;
	if (f->s->minimized &&
	    (st = wl_array_add(&a, sizeof(*st))) != NULL)
		*st = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED;
	if (f->s == C.focus && !f->s->minimized &&
	    (st = wl_array_add(&a, sizeof(*st))) != NULL)
		*st = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED;

	zwlr_foreign_toplevel_handle_v1_send_state(f->resource, &a);
	wl_array_release(&a);
}

/* Everything the protocol says a change must be followed by. */
static void
ftl_done(struct surface *s)
{
	struct ftl *f;

	wl_list_for_each(f, &s->ftl, link)
		zwlr_foreign_toplevel_handle_v1_send_done(f->resource);
}

static void
ftl_state_changed(struct surface *s)
{
	struct ftl *f;

	if (s == NULL || s->role != ROLE_TOPLEVEL)
		return;
	wl_list_for_each(f, &s->ftl, link) {
		ftl_send_state(f);
		zwlr_foreign_toplevel_handle_v1_send_done(f->resource);
	}
}

/*
 * Activation is not a property of one window: the one that loses focus has to
 * be told too, or a taskbar shows two windows highlighted at once.
 */
static void
ftl_focus_changed(void)
{
	struct surface *s;

	wl_list_for_each(s, &C.toplevels, link)
		ftl_state_changed(s);
}

static void
ftl_title_changed(struct surface *s)
{
	struct ftl *f;

	if (s->role != ROLE_TOPLEVEL)
		return;
	wl_list_for_each(f, &s->ftl, link)
		zwlr_foreign_toplevel_handle_v1_send_title(f->resource,
		    s->title);
	ftl_done(s);
}

static void
ftl_app_id_changed(struct surface *s)
{
	struct ftl *f;

	if (s->role != ROLE_TOPLEVEL)
		return;
	wl_list_for_each(f, &s->ftl, link)
		zwlr_foreign_toplevel_handle_v1_send_app_id(f->resource,
		    s->app_id);
	ftl_done(s);
}

/* The toplevel is going away: tell every manager, and orphan the handles. */
static void
ftl_closed(struct surface *s)
{
	struct ftl *f, *tmp;

	wl_list_for_each_safe(f, tmp, &s->ftl, link) {
		zwlr_foreign_toplevel_handle_v1_send_closed(f->resource);
		/*
		 * The client destroys the handle in its own time; until it does,
		 * the handle must not point at a freed surface.
		 */
		f->s = NULL;
		wl_list_remove(&f->link);
		wl_list_init(&f->link);
	}
}

static void
ftl_handle_destroy(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	wl_resource_destroy(r);
}

static void
ftl_handle_set_maximized(struct wl_client *c, struct wl_resource *r)
{
	struct ftl *f = wl_resource_get_user_data(r);
	struct surface *s;

	(void)c;
	if (f == NULL || (s = f->s) == NULL || s->maximized)
		return;

	s->mx = s->x; s->my = s->y; s->mw = s->w; s->mh = s->h;
	s->maximized = 1;
	s->x = 0;
	s->y = TITLEH;
	dmg_add_surface(s);
	send_configure(s, fbgui_width(C.fb),
	    fbgui_height(C.fb) - TITLEH, 0);
	ftl_state_changed(s);
}

static void
ftl_handle_unset_maximized(struct wl_client *c, struct wl_resource *r)
{
	struct ftl *f = wl_resource_get_user_data(r);
	struct surface *s;

	(void)c;
	if (f == NULL || (s = f->s) == NULL || !s->maximized)
		return;

	dmg_add_surface(s);
	s->maximized = 0;
	s->x = s->mx;
	s->y = s->my;
	send_configure(s, s->mw, s->mh, 0);
	ftl_state_changed(s);
}

static void
ftl_handle_set_minimized(struct wl_client *c, struct wl_resource *r)
{
	struct ftl *f = wl_resource_get_user_data(r);
	struct surface *s;

	(void)c;
	if (f == NULL || (s = f->s) == NULL || s->minimized)
		return;

	dmg_add_surface(s);		/* repaint what it was covering */
	s->minimized = 1;
	wlog("foreign-toplevel: minimize \"%s\"\n", s->title);
	if (C.focus == s)
		keyboard_focus(top_toplevel());	/* broadcasts for everyone */
	else
		ftl_state_changed(s);
}

static void
ftl_handle_unset_minimized(struct wl_client *c, struct wl_resource *r)
{
	struct ftl *f = wl_resource_get_user_data(r);
	struct surface *s;

	(void)c;
	if (f == NULL || (s = f->s) == NULL || !s->minimized)
		return;

	s->minimized = 0;
	dmg_add_surface(s);
	ftl_state_changed(s);
}

static void
ftl_handle_activate(struct wl_client *c, struct wl_resource *r,
    struct wl_resource *seat)
{
	struct ftl *f = wl_resource_get_user_data(r);
	struct surface *s;

	(void)c; (void)seat;
	if (f == NULL || (s = f->s) == NULL)
		return;

	s->minimized = 0;
	wlog("foreign-toplevel: activate \"%s\"\n", s->title);

	/* Raise: C.toplevels is bottom-to-top, so the tail is the top. */
	wl_list_remove(&s->link);
	wl_list_insert(C.toplevels.prev, &s->link);

	keyboard_focus(s);	/* which broadcasts the new activation state */
	dmg_add_surface(s);
}

static void
ftl_handle_close(struct wl_client *c, struct wl_resource *r)
{
	struct ftl *f = wl_resource_get_user_data(r);

	(void)c;
	if (f == NULL || f->s == NULL || f->s->xdg_toplevel == NULL)
		return;
	/*
	 * A request, not an order: xdg_toplevel.close asks the client to close.
	 * Whether it does is up to it.
	 */
	xdg_toplevel_send_close(f->s->xdg_toplevel);
}

static void
ftl_handle_set_rectangle(struct wl_client *c, struct wl_resource *r,
    struct wl_resource *surface, int32_t x, int32_t y, int32_t w, int32_t h)
{
	/* Where the taskbar button is, for a minimize animation.  We have none. */
	(void)c; (void)r; (void)surface; (void)x; (void)y; (void)w; (void)h;
}

static void
ftl_handle_set_fullscreen(struct wl_client *c, struct wl_resource *r,
    struct wl_resource *output)
{
	(void)output;
	/* wlcompd has no fullscreen of its own; maximizing is as close as it gets. */
	ftl_handle_set_maximized(c, r);
}

static void
ftl_handle_unset_fullscreen(struct wl_client *c, struct wl_resource *r)
{
	ftl_handle_unset_maximized(c, r);
}

static const struct zwlr_foreign_toplevel_handle_v1_interface ftl_handle_impl = {
	.set_maximized		= ftl_handle_set_maximized,
	.unset_maximized	= ftl_handle_unset_maximized,
	.set_minimized		= ftl_handle_set_minimized,
	.unset_minimized	= ftl_handle_unset_minimized,
	.activate		= ftl_handle_activate,
	.close			= ftl_handle_close,
	.set_rectangle		= ftl_handle_set_rectangle,
	.destroy		= ftl_handle_destroy,
	.set_fullscreen		= ftl_handle_set_fullscreen,
	.unset_fullscreen	= ftl_handle_unset_fullscreen,
};

static void
ftl_handle_res_destroy(struct wl_resource *r)
{
	struct ftl *f = wl_resource_get_user_data(r);

	if (f == NULL)
		return;
	wl_list_remove(&f->link);	/* safe: ftl_closed re-inits the link */
	free(f);
}

/* Tell one manager about one toplevel. */
static void
ftl_announce_to(struct wl_resource *mgr, struct surface *s)
{
	struct wl_client *client = wl_resource_get_client(mgr);
	struct wl_resource *out;
	struct ftl *f;

	if ((f = calloc(1, sizeof(*f))) == NULL)
		return;

	f->s = s;
	f->resource = wl_resource_create(client,
	    &zwlr_foreign_toplevel_handle_v1_interface,
	    wl_resource_get_version(mgr), 0);
	if (f->resource == NULL) {
		free(f);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(f->resource, &ftl_handle_impl, f,
	    ftl_handle_res_destroy);

	zwlr_foreign_toplevel_manager_v1_send_toplevel(mgr, f->resource);
	wl_list_insert(&s->ftl, &f->link);
	wlog("foreign-toplevel: announced \"%s\" (app_id \"%s\")\n",
	    s->title, s->app_id);

	zwlr_foreign_toplevel_handle_v1_send_title(f->resource, s->title);
	zwlr_foreign_toplevel_handle_v1_send_app_id(f->resource, s->app_id);
	if ((out = output_of(client)) != NULL)
		zwlr_foreign_toplevel_handle_v1_send_output_enter(f->resource,
		    out);
	ftl_send_state(f);
	zwlr_foreign_toplevel_handle_v1_send_done(f->resource);
}

/* A new toplevel appeared: tell every manager. */
static void
ftl_announce(struct surface *s)
{
	struct wl_resource *mgr;

	wl_resource_for_each(mgr, &C.ftl_managers)
		ftl_announce_to(mgr, s);
}

static void
ftl_manager_stop(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	zwlr_foreign_toplevel_manager_v1_send_finished(r);
	wl_resource_destroy(r);
}

static const struct zwlr_foreign_toplevel_manager_v1_interface ftl_manager_impl = {
	.stop = ftl_manager_stop,
};

static void
ftl_manager_res_destroy(struct wl_resource *r)
{
	wl_list_remove(wl_resource_get_link(r));
}

static void
bind_ftl_manager(struct wl_client *client, void *data, uint32_t version,
    uint32_t id)
{
	struct wl_resource *r;
	struct surface *s;

	(void)data;
	r = wl_resource_create(client,
	    &zwlr_foreign_toplevel_manager_v1_interface, version, id);
	if (r == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(r, &ftl_manager_impl, NULL,
	    ftl_manager_res_destroy);
	wl_list_insert(&C.ftl_managers, wl_resource_get_link(r));
	wlog("foreign-toplevel: manager bound (v%u)\n", version);

	/* A manager that binds late still has to learn about what already exists. */
	wl_list_for_each(s, &C.toplevels, link)
		if (s->mapped)
			ftl_announce_to(r, s);
}

/* ------------------------------------------------------- ext-workspace-v1 */

/*
 * Virtual desktops.
 *
 * The compositor half is small: every toplevel remembers which desktop it is on
 * (surface.ws), toplevel_visible() hides the ones that are not on the desktop
 * currently displayed (C.ws), and switching desktops is a repaint plus a
 * refocus.  The client half is this protocol, which is the only way a panel can
 * learn that desktops exist at all -- lxqt-panel's pager ("desktopswitch") drives
 * it.
 *
 * ext-workspace is double-buffered: a client asks for a workspace to be
 * activated and nothing happens until it sends manager.commit.  Hence the
 * pending state per manager.  Each manager that binds gets its own group handle
 * and its own handle per workspace; the objects are per-client, the desktops are
 * not.
 */

struct wsmgr {
	struct wl_resource	*resource;
	struct wl_resource	*group;
	struct wl_resource	*ws[NWORKSPACES];
	int			 pending;	/* workspace to activate, or -1 */
	struct wl_list		 link;		/* in C.ws_managers */
};

static void
ws_send_state(struct wsmgr *m, int i)
{
	if (m->ws[i] == NULL)
		return;
	ext_workspace_handle_v1_send_state(m->ws[i],
	    i == C.ws ? EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE : 0);
}

/* Tell every panel which desktop is on screen now. */
static void
ws_broadcast(void)
{
	struct wsmgr *m;
	int i;

	wl_list_for_each(m, &C.ws_managers, link) {
		for (i = 0; i < NWORKSPACES; i++)
			ws_send_state(m, i);
		ext_workspace_manager_v1_send_done(m->resource);
	}
}

static void
workspace_switch(int to)
{
	struct surface *s;

	if (to < 0 || to >= NWORKSPACES || to == C.ws)
		return;

	C.ws = to;

	/* Everything on screen changes at once. */
	dmg_add(0, 0, fbgui_width(C.fb), fbgui_height(C.fb));

	/*
	 * The focused window may have just vanished onto another desktop.  Give
	 * focus to the top window of the one we arrived at -- keyboard_focus()
	 * also tells the taskbar which window is active now.
	 */
	s = top_toplevel();
	if (C.focus != s)
		keyboard_focus(s);
	else
		ftl_focus_changed();

	ws_broadcast();

	{
		struct surface *t;
		int n = 0;

		wl_list_for_each(t, &C.toplevels, link)
			if (toplevel_visible(t))
				n++;
		wlog("workspace: switched to %d (%d windows visible)\n", to, n);
	}
}

/* --- ext_workspace_handle_v1 */

static void
wsh_destroy(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	wl_resource_destroy(r);
}

/* Which workspace is this handle?  Encoded in the user data as index+1. */
static int
wsh_index(struct wl_resource *r)
{
	return (int)(intptr_t)wl_resource_get_user_data(r) - 1;
}

static struct wsmgr *
wsh_manager(struct wl_resource *r)
{
	struct wsmgr *m;
	int i;

	wl_list_for_each(m, &C.ws_managers, link)
		for (i = 0; i < NWORKSPACES; i++)
			if (m->ws[i] == r)
				return m;
	return NULL;
}

static void
wsh_activate(struct wl_client *c, struct wl_resource *r)
{
	struct wsmgr *m = wsh_manager(r);

	(void)c;
	/* Double-buffered: it takes effect at manager.commit, not here. */
	if (m != NULL)
		m->pending = wsh_index(r);
}

static void
wsh_deactivate(struct wl_client *c, struct wl_resource *r)
{
	/*
	 * There is always exactly one desktop on screen, so "deactivate this one"
	 * has no meaning on its own -- something else has to become active, and
	 * that is what activate is for.
	 */
	(void)c; (void)r;
}

static void
wsh_assign(struct wl_client *c, struct wl_resource *r,
    struct wl_resource *group)
{
	/* We advertise no assign capability. */
	(void)c; (void)r; (void)group;
}

static void
wsh_remove(struct wl_client *c, struct wl_resource *r)
{
	/* Nor a remove one: the four desktops are fixed. */
	(void)c; (void)r;
}

static const struct ext_workspace_handle_v1_interface wsh_impl = {
	.destroy	= wsh_destroy,
	.activate	= wsh_activate,
	.deactivate	= wsh_deactivate,
	.assign		= wsh_assign,
	.remove		= wsh_remove,
};

/* --- ext_workspace_group_handle_v1 */

static void
wsg_create_workspace(struct wl_client *c, struct wl_resource *r,
    const char *name)
{
	/* We advertise no create_workspace capability. */
	(void)c; (void)r; (void)name;
}

static void
wsg_destroy(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	wl_resource_destroy(r);
}

static const struct ext_workspace_group_handle_v1_interface wsg_impl = {
	.create_workspace	= wsg_create_workspace,
	.destroy		= wsg_destroy,
};

/* --- ext_workspace_manager_v1 */

static void
wsm_commit(struct wl_client *c, struct wl_resource *r)
{
	struct wsmgr *m = wl_resource_get_user_data(r);

	(void)c;
	if (m == NULL || m->pending < 0)
		return;

	workspace_switch(m->pending);
	m->pending = -1;
}

static void
wsm_stop(struct wl_client *c, struct wl_resource *r)
{
	(void)c;
	ext_workspace_manager_v1_send_finished(r);
	wl_resource_destroy(r);
}

static const struct ext_workspace_manager_v1_interface wsm_impl = {
	.commit	= wsm_commit,
	.stop	= wsm_stop,
};

static void
wsm_res_destroy(struct wl_resource *r)
{
	struct wsmgr *m = wl_resource_get_user_data(r);

	if (m == NULL)
		return;
	wl_list_remove(&m->link);
	free(m);
}

static void
bind_ws_manager(struct wl_client *client, void *data, uint32_t version,
    uint32_t id)
{
	struct wl_resource *out;
	struct wsmgr *m;
	char buf[8];
	int i;

	(void)data;
	if ((m = calloc(1, sizeof(*m))) == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	m->pending = -1;

	m->resource = wl_resource_create(client,
	    &ext_workspace_manager_v1_interface, version, id);
	if (m->resource == NULL) {
		free(m);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(m->resource, &wsm_impl, m,
	    wsm_res_destroy);
	wl_list_insert(&C.ws_managers, &m->link);

	/* One group: one screen, one set of desktops. */
	m->group = wl_resource_create(client,
	    &ext_workspace_group_handle_v1_interface, version, 0);
	if (m->group == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(m->group, &wsg_impl, m, NULL);
	ext_workspace_manager_v1_send_workspace_group(m->resource, m->group);
	/* No capabilities: the desktops are ours, a client cannot add any. */
	ext_workspace_group_handle_v1_send_capabilities(m->group, 0);
	if ((out = output_of(client)) != NULL)
		ext_workspace_group_handle_v1_send_output_enter(m->group, out);

	for (i = 0; i < NWORKSPACES; i++) {
		struct wl_array coords;
		uint32_t *co;

		m->ws[i] = wl_resource_create(client,
		    &ext_workspace_handle_v1_interface, version, 0);
		if (m->ws[i] == NULL) {
			wl_client_post_no_memory(client);
			return;
		}
		wl_resource_set_implementation(m->ws[i], &wsh_impl,
		    (void *)(intptr_t)(i + 1), NULL);

		ext_workspace_manager_v1_send_workspace(m->resource, m->ws[i]);
		ext_workspace_group_handle_v1_send_workspace_enter(m->group,
		    m->ws[i]);

		snprintf(buf, sizeof(buf), "%d", i + 1);
		ext_workspace_handle_v1_send_id(m->ws[i], buf);
		ext_workspace_handle_v1_send_name(m->ws[i], buf);

		/* A one-dimensional row of desktops. */
		wl_array_init(&coords);
		if ((co = wl_array_add(&coords, sizeof(*co))) != NULL)
			*co = (uint32_t)i;
		ext_workspace_handle_v1_send_coordinates(m->ws[i], &coords);
		wl_array_release(&coords);

		ext_workspace_handle_v1_send_capabilities(m->ws[i],
		    EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_ACTIVATE);
		ws_send_state(m, i);
	}

	ext_workspace_manager_v1_send_done(m->resource);
	wlog("workspace: manager bound (%d desktops, current %d)\n",
	    NWORKSPACES, C.ws);
}

/* ----------------------------------------------------------------- input */

static int
in_grip(const struct surface *s, int sx, int sy)
{
	return sx >= s->x + s->w - RESIZEW && sx < s->x + s->w &&
	    sy >= s->y + TITLEH + s->h - RESIZEW &&
	    sy < s->y + TITLEH + s->h;
}

/* Hit-test a surface's children (topmost first), then the surface itself. */
static struct surface *
hit_tree(struct surface *s, int sx, int sy)
{
	struct surface *c, *hit;
	int ox, oy;

	/* Children are drawn after the parent, so they are on top: search them
	 * in reverse. */
	for (c = wl_container_of(s->children.prev, c, sibling);
	    &c->sibling != &s->children;
	    c = wl_container_of(c->sibling.prev, c, sibling)) {
		if (!c->mapped)
			continue;
		if ((hit = hit_tree(c, sx, sy)) != NULL)
			return hit;
	}

	if (!s->mapped || s->img == NULL)
		return NULL;

	surface_origin(s, &ox, &oy);
	if (sx >= ox && sx < ox + s->w && sy >= oy && sy < oy + s->h)
		return s;

	/* A toplevel also owns its title bar. */
	if (s->role == ROLE_TOPLEVEL &&
	    sx >= s->x && sx < s->x + s->w &&
	    sy >= s->y && sy < s->y + TITLEH)
		return s;

	return NULL;
}

/* Search one layer, topmost-first within it. */
static struct surface *
hit_layer(int layer, int sx, int sy)
{
	struct surface *s, *hit = NULL, *h;

	wl_list_for_each(s, &C.layers[layer], llink) {
		if (!s->mapped)
			continue;
		if ((h = hit_tree(s, sx, sy)) != NULL)
			hit = h;	/* later == higher */
	}
	return hit;
}

static struct surface *
surface_at(int sx, int sy, int *on_titlebar)
{
	struct surface *s, *hit = NULL;

	/*
	 * Search in reverse stacking order: whatever is drawn last is hit
	 * first, so a click on a panel does not fall through to the window
	 * beneath it.
	 */
	if ((hit = hit_layer(ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, sx, sy)) == NULL)
		hit = hit_layer(ZWLR_LAYER_SHELL_V1_LAYER_TOP, sx, sy);

	if (hit == NULL) {
		/* Toplevels are bottom-to-top, so the last hit is the topmost. */
		wl_list_for_each(s, &C.toplevels, link) {
			struct surface *h;

			if (!toplevel_visible(s))
				continue;
			if ((h = hit_tree(s, sx, sy)) != NULL)
				hit = h;
		}
	}

	if (hit == NULL &&
	    (hit = hit_layer(ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, sx, sy)) == NULL)
		hit = hit_layer(ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, sx, sy);

	if (on_titlebar != NULL)
		*on_titlebar = (hit != NULL && hit->role == ROLE_TOPLEVEL &&
		    sy < hit->y + TITLEH && sy >= hit->y);

	return hit;
}

/* The toplevel a surface ultimately belongs to. */
static struct surface *
root_of(struct surface *s)
{
	while (s != NULL && s->parent != NULL)
		s = s->parent;
	return s;
}

static void
pointer_focus(struct surface *s)
{
	struct wl_resource *p;
	uint32_t serial;
	int ox, oy;

	if (C.ptr_focus == s)
		return;

	if (C.ptr_focus != NULL && C.ptr_focus->resource != NULL) {
		serial = wl_display_next_serial(C.display);
		wl_resource_for_each(p, &C.pointers)
			if (wl_resource_get_client(p) ==
			    wl_resource_get_client(C.ptr_focus->resource))
				wl_pointer_send_leave(p, serial,
				    C.ptr_focus->resource);
	}

	C.ptr_focus = s;

	if (s != NULL && s->resource != NULL) {
		surface_origin(s, &ox, &oy);
		serial = wl_display_next_serial(C.display);
		wl_resource_for_each(p, &C.pointers)
			if (wl_resource_get_client(p) ==
			    wl_resource_get_client(s->resource))
				wl_pointer_send_enter(p, serial, s->resource,
				    wl_fixed_from_int(C.cx - ox),
				    wl_fixed_from_int(C.cy - oy));
	}
}

static void
keyboard_focus(struct surface *s)
{
	struct wl_resource *k;
	struct wl_array keys;
	uint32_t serial;

	if (C.focus == s)
		return;

	if (C.focus != NULL && C.focus->resource != NULL) {
		serial = wl_display_next_serial(C.display);
		wl_resource_for_each(k, &C.keyboards)
			if (wl_resource_get_client(k) ==
			    wl_resource_get_client(C.focus->resource))
				wl_keyboard_send_leave(k, serial,
				    C.focus->resource);
		dmg_add(C.focus->x, C.focus->y, C.focus->w, TITLEH);
	}

	C.focus = s;
	ftl_focus_changed();	/* the taskbar highlights the active window */

	if (s != NULL) {
		dmg_add(s->x, s->y, s->w, TITLEH);
		if (s->resource != NULL) {
			wl_array_init(&keys);
			serial = wl_display_next_serial(C.display);
			wl_resource_for_each(k, &C.keyboards)
				if (wl_resource_get_client(k) ==
				    wl_resource_get_client(s->resource))
					wl_keyboard_send_enter(k, serial,
					    s->resource, &keys);
			wl_array_release(&keys);
		}
	}
}

static void
send_modifiers(void)
{
	struct wl_resource *k;
	uint32_t depressed, latched, locked, group, serial;

	if (C.xkb_state == NULL)
		return;

	depressed = xkb_state_serialize_mods(C.xkb_state,
	    XKB_STATE_MODS_DEPRESSED);
	latched = xkb_state_serialize_mods(C.xkb_state, XKB_STATE_MODS_LATCHED);
	locked = xkb_state_serialize_mods(C.xkb_state, XKB_STATE_MODS_LOCKED);
	group = xkb_state_serialize_layout(C.xkb_state,
	    XKB_STATE_LAYOUT_EFFECTIVE);

	serial = wl_display_next_serial(C.display);
	wl_resource_for_each(k, &C.keyboards)
		wl_keyboard_send_modifiers(k, serial, depressed, latched,
		    locked, group);
}

static int
on_keyboard(int fd, uint32_t mask, void *data)
{
	struct input_event ev;
	struct wl_resource *k;

	(void)mask; (void)data;

	while (read(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
		uint16_t evdev;
		int press;
		uint32_t serial;

		if (ev.page != INPUT_PAGE_KEY)
			continue;

		/* HID usage -> evdev keycode; see hid_evdev.h. */
		if ((evdev = hid_key_to_evdev(ev.code)) == 0)
			continue;

		press = (ev.value != INPUT_RELEASE);

		if (C.xkb_state != NULL)
			xkb_state_update_key(C.xkb_state, evdev + 8,
			    press ? XKB_KEY_DOWN : XKB_KEY_UP);

		if (C.focus != NULL && C.focus->resource != NULL) {
			serial = wl_display_next_serial(C.display);
			wl_resource_for_each(k, &C.keyboards)
				if (wl_resource_get_client(k) ==
				    wl_resource_get_client(C.focus->resource))
					wl_keyboard_send_key(k, serial,
					    now_ms(), evdev,
					    press ? WL_KEYBOARD_KEY_STATE_PRESSED
					          : WL_KEYBOARD_KEY_STATE_RELEASED);
		}
		send_modifiers();
	}

	return 0;
}

static int
on_mouse(int fd, uint32_t mask, void *data)
{
	/*
	 * Seed the button state with what we already hold.  fbgui_read_mouse()
	 * only *changes* the bitmap when a button event is in the batch -- a
	 * batch of pure motion leaves it alone -- so starting from zero would
	 * read every drag as "button released" and settle it on the first move.
	 */
	int dx = 0, dy = 0, buttons = C.buttons, nx, ny, bar;
	struct surface *s, *root;
	struct wl_resource *p;
	uint32_t serial;
	int ox, oy;

	(void)mask; (void)data;

	if (fbgui_read_mouse(fd, &dx, &dy, &buttons) <= 0)
		return 0;

	nx = C.cx + dx;
	ny = C.cy - dy;			/* the mouse's Y grows upward */
	if (nx < 0) nx = 0;
	if (ny < 0) ny = 0;
	if (nx >= fbgui_width(C.fb)) nx = fbgui_width(C.fb) - 1;
	if (ny >= fbgui_height(C.fb)) ny = fbgui_height(C.fb) - 1;

	if (nx != C.cx || ny != C.cy) {
		dmg_add_cursor(C.cx, C.cy);	/* erase it where it was */
		C.cx = nx;
		C.cy = ny;
		dmg_add_cursor(C.cx, C.cy);	/* draw it where it is */
	}

	/*
	 * A resize does not move pixels: we tell the client the size we want and
	 * it answers with a buffer of that size.  That round trip is what
	 * xdg_shell means by a resize.
	 */
	if (C.resizing != NULL) {
		struct surface *rs = C.resizing;

		if (!(buttons & 1)) {
			send_configure(rs, rs->cfg_w, rs->cfg_h, 0);
			C.resizing = NULL;
		} else {
			int nw = C.rz_w0 + (C.cx - C.rz_cx0);
			int nh = C.rz_h0 + (C.cy - C.rz_cy0);

			if (nw < MINW) nw = MINW;
			if (nh < MINH) nh = MINH;

			/* Only when it changes: a configure per mouse event
			 * would flood the client. */
			if (nw != rs->cfg_w || nh != rs->cfg_h)
				send_configure(rs, nw, nh, 1);
			C.buttons = buttons;
			return 0;
		}
	}

	if (C.dragging != NULL) {
		if (!(buttons & 1)) {
			C.dragging = NULL;
		} else {
			dmg_add_surface(C.dragging);	/* erase the old place */
			C.dragging->x = C.cx - C.drag_dx;
			C.dragging->y = C.cy - C.drag_dy;
			dmg_add_surface(C.dragging);	/* draw the new one */
			C.buttons = buttons;
			return 0;
		}
	}

	s = surface_at(C.cx, C.cy, &bar);
	root = root_of(s);

	if (buttons != C.buttons) {
		int pressed = (buttons & 1) && !(C.buttons & 1);
		int released = !(buttons & 1) && (C.buttons & 1);

		if (pressed) {
			struct surface *t, *tmp;

			wlog("button press at %d,%d (%s)\n", C.cx, C.cy,
			    s == NULL ? "desktop" :
			    s->role == ROLE_POPUP ? "popup" :
			    s->role == ROLE_SUBSURFACE ? "subsurface" :
			    bar ? "title bar" :
			    in_grip(s, C.cx, C.cy) ? "grip" : "content");

			/*
			 * A click anywhere but inside a popup dismisses the
			 * open popups -- that is what makes a menu close when
			 * you click away from it.
			 */
			if (s == NULL || s->role != ROLE_POPUP) {
				wl_list_for_each_safe(t, tmp, &C.toplevels, link)
					dismiss_popups_of(t);
			}
		}

		/*
		 * A panel that asked for keyboard interactivity takes focus when
		 * it is clicked -- that is what "on demand" means, and without it
		 * a panel with a search box could never be typed into.
		 */
		if (pressed && root != NULL && root->role == ROLE_LAYER &&
		    root->lkbd != ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE)
			keyboard_focus(root);

		if (pressed && root != NULL && root->role == ROLE_TOPLEVEL) {
			keyboard_focus(root);
			wl_list_remove(&root->link);	/* raise */
			wl_list_insert(C.toplevels.prev, &root->link);
			dmg_add_surface(root);

			if (s == root && !bar && in_grip(root, C.cx, C.cy)) {
				C.resizing = root;
				C.rz_cx0 = C.cx;
				C.rz_cy0 = C.cy;
				C.rz_w0 = root->w;
				C.rz_h0 = root->h;
				C.buttons = buttons;
				wlog("resize started from %dx%d\n", root->w,
				    root->h);
				return 0;
			}

			if (bar) {
				if (C.cx >= root->x + root->w - CLOSEW) {
					if (root->xdg_toplevel != NULL)
						xdg_toplevel_send_close(
						    root->xdg_toplevel);
				} else {
					C.dragging = root;
					C.drag_dx = C.cx - root->x;
					C.drag_dy = C.cy - root->y;
				}
				C.buttons = buttons;
				return 0;
			}
		}

		if (!bar && s != NULL && (pressed || released)) {
			pointer_focus(s);
			serial = wl_display_next_serial(C.display);
			wl_resource_for_each(p, &C.pointers)
				if (wl_resource_get_client(p) ==
				    wl_resource_get_client(s->resource))
					wl_pointer_send_button(p, serial,
					    now_ms(), BTN_LEFT,
					    pressed
					      ? WL_POINTER_BUTTON_STATE_PRESSED
					      : WL_POINTER_BUTTON_STATE_RELEASED);
		}
		C.buttons = buttons;
	}

	if (!bar && s != NULL) {
		pointer_focus(s);
		surface_origin(s, &ox, &oy);
		wl_resource_for_each(p, &C.pointers)
			if (wl_resource_get_client(p) ==
			    wl_resource_get_client(s->resource))
				wl_pointer_send_motion(p, now_ms(),
				    wl_fixed_from_int(C.cx - ox),
				    wl_fixed_from_int(C.cy - oy));
	} else {
		pointer_focus(NULL);
	}

	return 0;
}

/* ------------------------------------------------------------------ setup */

/*
 * The keymap goes to clients as a file descriptor.  It has to be a *real* file:
 * a client maps it MAP_PRIVATE, and on MINIX only a genuine file mapping
 * carries its contents that way -- an shm token would map the empty token file
 * and the client would compile a keymap out of zeroes.
 */
static int
open_keymap(size_t *sizep)
{
	char tmp[] = "/tmp/wlcompd-keymap.XXXXXX";
	struct stat st;
	char *text;
	FILE *f;
	int fd;

	if ((f = fopen(KEYMAP_PATH, "r")) == NULL) {
		wlog("no keymap at %s: %s\n", KEYMAP_PATH, strerror(errno));
		return -1;
	}
	if (stat(KEYMAP_PATH, &st) != 0 ||
	    (text = malloc(st.st_size + 1)) == NULL) {
		fclose(f);
		return -1;
	}
	if (fread(text, 1, st.st_size, f) != (size_t)st.st_size) {
		free(text);
		fclose(f);
		return -1;
	}
	text[st.st_size] = '\0';
	fclose(f);

	if ((fd = mkstemp(tmp)) < 0) {
		free(text);
		return -1;
	}
	(void)unlink(tmp);		/* the fd is all anyone needs */

	if (write(fd, text, st.st_size) != (ssize_t)st.st_size) {
		close(fd);
		free(text);
		return -1;
	}

	/* Compile it for ourselves too, so the modifiers we report come from
	 * the same keymap the clients were given. */
	C.keymap = xkb_keymap_new_from_string(C.xkb, text,
	    XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	free(text);
	if (C.keymap == NULL) {
		wlog("keymap failed to compile\n");
		close(fd);
		return -1;
	}
	if ((C.xkb_state = xkb_state_new(C.keymap)) == NULL) {
		close(fd);
		return -1;
	}

	*sizep = (size_t)st.st_size;
	return fd;
}

static int
open_kbd_device(void)
{
	int fd = open("/dev/kbdmux", O_RDONLY | O_NONBLOCK);

	if (fd < 0)
		fd = open("/dev/kbd0", O_RDONLY | O_NONBLOCK);
	return fd;
}

int
main(int argc, char **argv)
{
	const char *font, *sock;
	int mfd, kfd;

	lg = stdout;
	setvbuf(lg, NULL, _IONBF, 0);

	wl_list_init(&C.toplevels);
	wl_list_init(&C.outputs);
	wl_list_init(&C.ftl_managers);
	wl_list_init(&C.ws_managers);
	for (int i = 0; i < NLAYERS; i++)
		wl_list_init(&C.layers[i]);
	wl_list_init(&C.pointers);
	wl_list_init(&C.keyboards);
	wl_list_init(&C.data_devices);
	pixman_region32_init(&C.damage);
	C.keymap_fd = -1;

	if ((C.fb = fbgui_open()) == NULL) {
		wlog("fbgui_open failed (no /dev/fb0?)\n");
		return 1;
	}

	font = (argc > 1) ? argv[1] : FONT_DEFAULT;
	if (fbgui_load_font(C.fb, font) != 0)
		wlog("warning: no font (%s) - titles will be blank\n", font);

	C.cx = fbgui_width(C.fb) / 2;
	C.cy = fbgui_height(C.fb) / 2;

	if ((C.display = wl_display_create()) == NULL) {
		wlog("wl_display_create failed\n");
		return 1;
	}
	C.loop = wl_display_get_event_loop(C.display);

	if (wl_display_init_shm(C.display) != 0) {
		wlog("wl_display_init_shm failed\n");
		return 1;
	}

	if (wl_global_create(C.display, &wl_compositor_interface, 4, NULL,
		bind_compositor) == NULL ||
	    wl_global_create(C.display, &wl_subcompositor_interface, 1, NULL,
		bind_subcompositor) == NULL ||
	    wl_global_create(C.display, &wl_seat_interface, 5, NULL,
		bind_seat) == NULL ||
	    wl_global_create(C.display, &wl_output_interface, 3, NULL,
		bind_output) == NULL ||
	    wl_global_create(C.display, &wl_data_device_manager_interface, 3,
		NULL, bind_ddm) == NULL ||
	    wl_global_create(C.display, &zwlr_layer_shell_v1_interface, 4, NULL,
		bind_layer_shell) == NULL ||
	    /*
	     * How a taskbar gets to see other clients' windows.  Nothing else in
	     * Wayland will tell it they exist.
	     */
	    wl_global_create(C.display,
		&zwlr_foreign_toplevel_manager_v1_interface, 3, NULL,
		bind_ftl_manager) == NULL ||
	    /* Virtual desktops.  Nothing else in Wayland exposes them. */
	    wl_global_create(C.display, &ext_workspace_manager_v1_interface, 1,
		NULL, bind_ws_manager) == NULL ||
	    wl_global_create(C.display, &xdg_wm_base_interface, 3, NULL,
		bind_wm_base) == NULL ||
	    /* Server-side decorations, so clients do not draw their own on top. */
	    wl_global_create(C.display, &zxdg_decoration_manager_v1_interface, 1,
		NULL, bind_deco_manager) == NULL) {
		wlog("wl_global_create failed\n");
		return 1;
	}

	C.xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (C.xkb != NULL)
		C.keymap_fd = open_keymap(&C.keymap_size);
	if (C.keymap_fd < 0)
		wlog("warning: no keymap - clients will get NO_KEYMAP\n");

	if ((sock = wl_display_add_socket_auto(C.display)) == NULL) {
		wlog("wl_display_add_socket_auto failed: %s\n", strerror(errno));
		return 1;
	}

	mfd = fbgui_open_mouse();
	kfd = open_kbd_device();

	if (mfd >= 0)
		wl_event_loop_add_fd(C.loop, mfd, WL_EVENT_READABLE, on_mouse,
		    NULL);
	if (kfd >= 0)
		wl_event_loop_add_fd(C.loop, kfd, WL_EVENT_READABLE,
		    on_keyboard, NULL);

	C.repaint = wl_event_loop_add_timer(C.loop, on_repaint, NULL);
	wl_event_source_timer_update(C.repaint, REPAINT_MS);

	dmg_add_all();			/* the first frame is the whole screen */
	recomposite();
	fbgui_present_full(C.fb);

	wlog("WLCOMPD READY fb=%dx%d socket=%s mouse=%s kbd=%s keymap=%s\n",
	    fbgui_width(C.fb), fbgui_height(C.fb), sock,
	    mfd >= 0 ? "yes" : "no", kfd >= 0 ? "yes" : "no",
	    C.keymap_fd >= 0 ? "yes" : "no");

	wl_display_run(C.display);

	wl_display_destroy(C.display);
	fbgui_close(C.fb);
	return 0;
}
