/*	wlcompd - a Wayland compositor for MINIX
 *
 * The display end of the stack the rest of this branch was built for.  It owns
 * /dev/fb0 through libfbgui (pixman for compositing, FreeType for the title
 * bars) and speaks Wayland to its clients: wl_compositor, wl_shm, wl_seat,
 * wl_output and xdg_shell.
 *
 * Everything underneath it has been proved separately, on-target, and the shape
 * of this file follows from that:
 *
 *   - a client's pixels arrive as a POSIX shm pool (wlprobe: shm_open, a
 *     writable MAP_SHARED mapping, and an fd passed over SCM_RIGHTS that the
 *     receiver really shares rather than copies),
 *   - every request it makes is dispatched through libffi (ffiprobe),
 *   - the event loop runs on MINIX's poll(2) emulation of epoll/timerfd/
 *     signalfd/eventfd (wlcoreprobe), and
 *   - keys are translated with libxkbcommon against the keymap we ship
 *     (xkbprobe).
 *
 * Two things are worth knowing before reading further.
 *
 * Keycodes.  MINIX's input server speaks USB HID usages; Wayland speaks evdev.
 * They are not the same and not a fixed offset apart (HID orders letters
 * alphabetically, evdev by QWERTY position), so every key goes through
 * hid_evdev.h.  Get this wrong and the keymap silently produces the wrong
 * letters.
 *
 * Client buffers.  A committed buffer is copied into memory of our own before
 * it is composited, and released immediately.  Compositing straight out of the
 * client's pool would mean rendering from memory another process can rewrite
 * underneath us -- and, on MINIX, could not be defended with the usual SIGBUS
 * trick, which needs SA_SIGINFO (see the note in wayland-shm.c).
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <assert.h>
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
#define CURW		12		/* cursor */
#define CURH		19
#define MAXSURF		32
#define REPAINT_MS	16		/* ~60 Hz cap on recompositing */

#define FONT_DEFAULT	"/usr/share/fonts/TTF/DejaVuSans.ttf"

/* ------------------------------------------------------------------ state */

struct surface {
	struct wl_resource	*resource;
	struct wl_resource	*xdg_surface;
	struct wl_resource	*xdg_toplevel;

	/* Pending (attached but not yet committed) buffer. */
	struct wl_resource	*pending_buffer;
	int			 pending_buffer_set;

	/* Our own copy of the last committed buffer.  Never the client's. */
	uint32_t		*pix;
	pixman_image_t		*img;
	int			 w, h;

	int			 x, y;		/* title-bar top-left */
	int			 mapped;
	char			 title[64];

	struct wl_list		 frame_callbacks;	/* wl_resource link */
	struct wl_list		 link;			/* comp.surfaces */
};

static struct {
	struct wl_display	*display;
	struct wl_event_loop	*loop;
	fbgui_t			*fb;

	struct wl_list		 surfaces;	/* bottom-to-top */
	struct surface		*focus;

	/* Seat */
	struct wl_list		 pointers;	/* wl_resource link */
	struct wl_list		 keyboards;
	int			 cx, cy;	/* cursor position */
	int			 buttons;
	struct surface		*ptr_focus;

	/* Keyboard state */
	struct xkb_context	*xkb;
	struct xkb_keymap	*keymap;
	struct xkb_state	*xkb_state;
	int			 keymap_fd;
	size_t			 keymap_size;

	/* Window drag */
	struct surface		*dragging;
	int			 drag_dx, drag_dy;

	int			 dirty;
	struct wl_event_source	*repaint;
} C;

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

/* ------------------------------------------------------------- rendering */

static const pixman_color_t c_desk  = { 0x1c1c, 0x2222, 0x2a2a, 0xffff };
static const pixman_color_t c_title = { 0x3a3a, 0x5a5a, 0x8888, 0xffff };
static const pixman_color_t c_tfoc  = { 0x4a4a, 0x8a8a, 0xcccc, 0xffff };
static const pixman_color_t c_ttext = { 0xffff, 0xffff, 0xffff, 0xffff };
static const pixman_color_t c_cur   = { 0xffff, 0xffff, 0xffff, 0xffff };

static struct surface *
top_surface(void)
{
	struct surface *s, *top = NULL;

	wl_list_for_each(s, &C.surfaces, link)
		if (s->mapped)
			top = s;		/* list is bottom-to-top */
	return top;
}

static void
recomposite(void)
{
	struct surface *s;

	fbgui_fill_rect(C.fb, &c_desk, 0, 0,
	    fbgui_width(C.fb), fbgui_height(C.fb));

	wl_list_for_each(s, &C.surfaces, link) {
		const pixman_color_t *tc;

		if (!s->mapped || s->img == NULL)
			continue;

		tc = (s == C.focus) ? &c_tfoc : &c_title;
		fbgui_fill_rect(C.fb, tc, s->x, s->y, s->w, TITLEH);
		if (s->title[0] != '\0')
			fbgui_draw_text(C.fb, s->x + 8, s->y + 16, s->title,
			    &c_ttext, 14);
		fbgui_draw_text(C.fb, s->x + s->w - CLOSEW + 6, s->y + 16, "x",
		    &c_ttext, 14);

		pixman_image_composite32(PIXMAN_OP_SRC, s->img, NULL,
		    fbgui_surface(C.fb), 0, 0, 0, 0,
		    s->x, s->y + TITLEH, s->w, s->h);
	}

	fbgui_fill_rect(C.fb, &c_cur, C.cx, C.cy, CURW, CURH);

	fbgui_damage_all(C.fb);
	fbgui_present(C.fb);
}

/*
 * Frame callbacks tell a client it may draw again.  Firing them is what keeps
 * an animating client running, so they are sent every repaint whether or not
 * anything of ours changed.
 */
static void
send_frame_callbacks(void)
{
	struct surface *s;
	uint32_t t = now_ms();

	wl_list_for_each(s, &C.surfaces, link) {
		struct wl_resource *cb, *tmp;

		wl_resource_for_each_safe(cb, tmp, &s->frame_callbacks) {
			wl_callback_send_done(cb, t);
			wl_resource_destroy(cb);
		}
	}
}

static int
on_repaint(void *data)
{
	(void)data;

	if (C.dirty) {
		recomposite();
		C.dirty = 0;
	}
	send_frame_callbacks();

	wl_event_source_timer_update(C.repaint, REPAINT_MS);
	return 0;
}

/* -------------------------------------------------------------- wl_buffer */

/*
 * Take a private copy of the client's committed buffer.  See the note at the
 * top: we never composite out of the client's pool.
 */
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
		s->img = pixman_image_create_bits(PIXMAN_x8r8g8b8, w, h,
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
	 * Report a checksum of exactly what we received.  The client prints the
	 * checksum of what it painted; if the two agree, its pixels crossed the
	 * process boundary through the shm pool intact -- which is the whole
	 * point of the wl_shm path, and the one thing a screenshot could not
	 * tell us on a headless machine.
	 */
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

static void
surface_destroy(struct wl_resource *resource)
{
	struct surface *s = wl_resource_get_user_data(resource);
	struct wl_resource *cb, *tmp;

	if (s == NULL)
		return;

	wl_resource_for_each_safe(cb, tmp, &s->frame_callbacks)
		wl_resource_destroy(cb);

	wl_list_remove(&s->link);
	if (s->img != NULL)
		pixman_image_unref(s->img);
	free(s->pix);

	if (C.focus == s)
		C.focus = top_surface();
	if (C.ptr_focus == s)
		C.ptr_focus = NULL;
	if (C.dragging == s)
		C.dragging = NULL;

	free(s);
	C.dirty = 1;
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
	/* We repaint whole surfaces; damage rectangles are advisory here. */
	(void)c; (void)r; (void)x; (void)y; (void)w; (void)h;
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
surf_set_opaque_region(struct wl_client *c, struct wl_resource *r,
    struct wl_resource *region)
{
	(void)c; (void)r; (void)region;
}

static void
surf_set_input_region(struct wl_client *c, struct wl_resource *r,
    struct wl_resource *region)
{
	(void)c; (void)r; (void)region;
}

static void
surf_commit(struct wl_client *c, struct wl_resource *r)
{
	struct surface *s = wl_resource_get_user_data(r);

	(void)c;

	if (!s->pending_buffer_set)
		return;			/* commit with no new buffer */
	s->pending_buffer_set = 0;

	if (s->pending_buffer == NULL) {	/* attach(NULL): unmap */
		s->mapped = 0;
		C.dirty = 1;
		return;
	}

	if (surface_take_buffer(s, s->pending_buffer) == 0) {
		if (!s->mapped) {
			s->mapped = 1;
			C.focus = s;
			wlog("surface mapped %dx%d \"%s\"\n", s->w, s->h,
			    s->title);
		}
		C.dirty = 1;
	}

	/* Released at once: the client may reuse the buffer, and we already
	 * hold our own copy of it. */
	wl_buffer_send_release(s->pending_buffer);
	s->pending_buffer = NULL;
}

static void
surf_set_buffer_transform(struct wl_client *c, struct wl_resource *r,
    int32_t transform)
{
	(void)c; (void)r; (void)transform;
}

static void
surf_set_buffer_scale(struct wl_client *c, struct wl_resource *r,
    int32_t scale)
{
	(void)c; (void)r; (void)scale;
}

static void
surf_damage_buffer(struct wl_client *c, struct wl_resource *r,
    int32_t x, int32_t y, int32_t w, int32_t h)
{
	(void)c; (void)r; (void)x; (void)y; (void)w; (void)h;
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
	.set_opaque_region	= surf_set_opaque_region,
	.set_input_region	= surf_set_input_region,
	.commit			= surf_commit,
	.set_buffer_transform	= surf_set_buffer_transform,
	.set_buffer_scale	= surf_set_buffer_scale,
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
region_add(struct wl_client *c, struct wl_resource *r,
    int32_t x, int32_t y, int32_t w, int32_t h)
{
	(void)c; (void)r; (void)x; (void)y; (void)w; (void)h;
}

static void
region_subtract(struct wl_client *c, struct wl_resource *r,
    int32_t x, int32_t y, int32_t w, int32_t h)
{
	(void)c; (void)r; (void)x; (void)y; (void)w; (void)h;
}

static const struct wl_region_interface region_impl = {
	.destroy	= region_destroy,
	.add		= region_add,
	.subtract	= region_subtract,
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

	s->resource = wl_resource_create(client, &wl_surface_interface,
	    wl_resource_get_version(r), id);
	if (s->resource == NULL) {
		free(s);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(s->resource, &surface_impl, s,
	    surface_destroy);

	s->x = spawn_x;
	s->y = spawn_y;
	spawn_x = 60 + ((spawn_x + 30) % 240);
	spawn_y = 60 + ((spawn_y + 30) % 180);

	wl_list_insert(C.surfaces.prev, &s->link);	/* on top */
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
		C.dirty = 1;
	}
}

static void
xdgtop_set_app_id(struct wl_client *c, struct wl_resource *r, const char *id)
{
	(void)c; (void)r; (void)id;
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
xdgtop_noop_seat(struct wl_client *c, struct wl_resource *r,
    struct wl_resource *seat, uint32_t serial, uint32_t edges)
{
	(void)c; (void)r; (void)seat; (void)serial; (void)edges;
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

/* Its own signature -- casting xdgtop_noop to it would be undefined. */
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
	.resize			= xdgtop_noop_seat,
	.set_max_size		= xdgtop_set_size,
	.set_min_size		= xdgtop_set_size,
	.set_maximized		= xdgtop_noop,
	.unset_maximized	= xdgtop_noop,
	.set_fullscreen		= xdgtop_set_fullscreen,
	.unset_fullscreen	= xdgtop_noop,
	.set_minimized		= xdgtop_noop,
};

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
	struct wl_array states;
	uint32_t *st;

	s->xdg_toplevel = wl_resource_create(c, &xdg_toplevel_interface,
	    wl_resource_get_version(r), id);
	if (s->xdg_toplevel == NULL) {
		wl_client_post_no_memory(c);
		return;
	}
	wl_resource_set_implementation(s->xdg_toplevel, &xdg_toplevel_impl, s,
	    NULL);

	/*
	 * The handshake xdg_shell requires: propose a size, then configure the
	 * xdg_surface.  A client must not draw until it has seen this and
	 * ack'ed it, so getting it wrong shows up as a client that never
	 * produces a frame.  A zero width/height means "you choose".
	 */
	wl_array_init(&states);
	st = wl_array_add(&states, sizeof(uint32_t));
	if (st != NULL)
		*st = XDG_TOPLEVEL_STATE_ACTIVATED;
	xdg_toplevel_send_configure(s->xdg_toplevel, 0, 0, &states);
	wl_array_release(&states);

	xdg_surface_send_configure(s->xdg_surface,
	    wl_display_next_serial(C.display));

	wlog("xdg_toplevel created\n");
}

static void
xdgsurf_get_popup(struct wl_client *c, struct wl_resource *r, uint32_t id,
    struct wl_resource *parent, struct wl_resource *positioner)
{
	/* Popups are not composited, but the resource must exist or a client
	 * that asks for a menu dies on a protocol error rather than simply
	 * getting nothing. */
	struct wl_resource *res;

	(void)parent; (void)positioner;
	res = wl_resource_create(c, &xdg_popup_interface,
	    wl_resource_get_version(r), id);
	if (res == NULL) {
		wl_client_post_no_memory(c);
		return;
	}
	wl_resource_set_implementation(res, NULL, NULL, NULL);
	wlog("xdg_popup requested (not composited)\n");
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

	res = wl_resource_create(c, &xdg_positioner_interface,
	    wl_resource_get_version(r), id);
	if (res == NULL) {
		wl_client_post_no_memory(c);
		return;
	}
	wl_resource_set_implementation(res, NULL, NULL, NULL);
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

/* ---------------------------------------------------------------- wl_seat */

static void
seat_resource_destroy(struct wl_resource *r)
{
	wl_list_remove(wl_resource_get_link(r));
}

static void
pointer_set_cursor(struct wl_client *c, struct wl_resource *r, uint32_t serial,
    struct wl_resource *surface, int32_t hx, int32_t hy)
{
	/* We draw our own cursor; a client-supplied one is ignored. */
	(void)c; (void)r; (void)serial; (void)surface; (void)hx; (void)hy;
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

	/*
	 * Hand the client the keymap.  It arrives as a file descriptor the
	 * client mmap()s and feeds to xkb_keymap_new_from_string() -- which is
	 * exactly the path xkbprobe exercises, and the reason xkeyboard-config
	 * need not exist on this machine at all.
	 */
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
	wl_resource_set_implementation(r, &output_impl, NULL, NULL);

	wl_output_send_geometry(r, 0, 0, w, h, WL_OUTPUT_SUBPIXEL_UNKNOWN,
	    "MINIX", "fb0", WL_OUTPUT_TRANSFORM_NORMAL);
	wl_output_send_mode(r,
	    WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED, w, h, 60000);
	if (version >= WL_OUTPUT_SCALE_SINCE_VERSION)
		wl_output_send_scale(r, 1);
	if (version >= WL_OUTPUT_DONE_SINCE_VERSION)
		wl_output_send_done(r);
}

/* ----------------------------------------------------------------- input */

static struct surface *
surface_at(int sx, int sy, int *on_titlebar)
{
	struct surface *s, *hit = NULL;
	int bar = 0;

	wl_list_for_each(s, &C.surfaces, link) {
		if (!s->mapped)
			continue;
		if (sx < s->x || sx >= s->x + s->w)
			continue;
		if (sy < s->y || sy >= s->y + TITLEH + s->h)
			continue;
		hit = s;			/* later == higher */
		bar = (sy < s->y + TITLEH);
	}
	if (on_titlebar != NULL)
		*on_titlebar = bar;
	return hit;
}

static void
pointer_focus(struct surface *s)
{
	struct wl_resource *p;
	uint32_t serial;

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
		serial = wl_display_next_serial(C.display);
		wl_resource_for_each(p, &C.pointers)
			if (wl_resource_get_client(p) ==
			    wl_resource_get_client(s->resource))
				wl_pointer_send_enter(p, serial, s->resource,
				    wl_fixed_from_int(C.cx - s->x),
				    wl_fixed_from_int(C.cy - s->y - TITLEH));
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
	}

	C.focus = s;
	C.dirty = 1;

	if (s != NULL && s->resource != NULL) {
		wl_array_init(&keys);
		serial = wl_display_next_serial(C.display);
		wl_resource_for_each(k, &C.keyboards)
			if (wl_resource_get_client(k) ==
			    wl_resource_get_client(s->resource))
				wl_keyboard_send_enter(k, serial, s->resource,
				    &keys);
		wl_array_release(&keys);
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

		/* Our own xkb state, so modifiers we report stay in step with
		 * the keys we forward. */
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
	int dx = 0, dy = 0, buttons = 0, nx, ny, bar;
	struct surface *s;
	struct wl_resource *p;
	uint32_t serial;

	(void)mask; (void)data;

	if (fbgui_read_mouse(fd, &dx, &dy, &buttons) <= 0)
		return 0;

	nx = C.cx + dx;
	ny = C.cy - dy;			/* mouse Y grows upward */
	if (nx < 0) nx = 0;
	if (ny < 0) ny = 0;
	if (nx >= fbgui_width(C.fb)) nx = fbgui_width(C.fb) - 1;
	if (ny >= fbgui_height(C.fb)) ny = fbgui_height(C.fb) - 1;

	if (nx != C.cx || ny != C.cy) {
		C.cx = nx;
		C.cy = ny;
		C.dirty = 1;
	}

	/* Dragging a title bar moves the window; nothing reaches the client. */
	if (C.dragging != NULL) {
		if (!(buttons & 1)) {
			C.dragging = NULL;
		} else {
			C.dragging->x = C.cx - C.drag_dx;
			C.dragging->y = C.cy - C.drag_dy;
			C.dirty = 1;
			return 0;
		}
	}

	s = surface_at(C.cx, C.cy, &bar);

	if (buttons != C.buttons) {
		int pressed = (buttons & 1) && !(C.buttons & 1);
		int released = !(buttons & 1) && (C.buttons & 1);

		if (pressed && s != NULL) {
			keyboard_focus(s);
			/* Raise. */
			wl_list_remove(&s->link);
			wl_list_insert(C.surfaces.prev, &s->link);
			C.dirty = 1;

			if (bar) {
				if (C.cx >= s->x + s->w - CLOSEW) {
					if (s->xdg_toplevel != NULL)
						xdg_toplevel_send_close(
						    s->xdg_toplevel);
				} else {
					C.dragging = s;
					C.drag_dx = C.cx - s->x;
					C.drag_dy = C.cy - s->y;
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
		wl_resource_for_each(p, &C.pointers)
			if (wl_resource_get_client(p) ==
			    wl_resource_get_client(s->resource))
				wl_pointer_send_motion(p, now_ms(),
				    wl_fixed_from_int(C.cx - s->x),
				    wl_fixed_from_int(C.cy - s->y - TITLEH));
	} else {
		pointer_focus(NULL);
	}

	return 0;
}

/* ------------------------------------------------------------------ setup */

/*
 * The keymap goes to clients as a file descriptor.  It has to be a *real* file:
 * a client maps it with MAP_PRIVATE, and on MINIX only a genuine file mapping
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
	int fd, kfd;

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

	/* Compile it for ourselves too, so the modifiers we report to clients
	 * come from the same keymap they were given. */
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
	kfd = fd;
	return kfd;
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

	wl_list_init(&C.surfaces);
	wl_list_init(&C.pointers);
	wl_list_init(&C.keyboards);
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
	    wl_global_create(C.display, &wl_seat_interface, 5, NULL,
		bind_seat) == NULL ||
	    wl_global_create(C.display, &wl_output_interface, 3, NULL,
		bind_output) == NULL ||
	    wl_global_create(C.display, &xdg_wm_base_interface, 3, NULL,
		bind_wm_base) == NULL) {
		wlog("wl_global_create failed\n");
		return 1;
	}

	C.xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (C.xkb != NULL)
		C.keymap_fd = open_keymap(&C.keymap_size);
	if (C.keymap_fd < 0)
		wlog("warning: no keymap - clients will get NO_KEYMAP\n");

	if ((sock = wl_display_add_socket_auto(C.display)) == NULL) {
		wlog("wl_display_add_socket_auto failed: %s\n",
		    strerror(errno));
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

	C.dirty = 1;
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
