/*	wlclient - a Wayland client, and the end-to-end proof of the stack
 *
 * Connects to wlcompd, binds the globals, makes an xdg_toplevel, draws into a
 * wl_shm buffer and commits it -- then honours whatever size the compositor
 * configures it to, which is the other half of xdg_shell.
 *
 * That sequence exercises every piece this branch added, in the arrangement
 * they were built for: shm_open + MAP_SHARED for the pool, its fd passed to the
 * compositor over SCM_RIGHTS (wl_shm), requests marshalled and dispatched
 * through libffi, the compositor's event loop on the poll(2) emulation, and the
 * keymap it sends compiled by libxkbcommon.  A resize adds wl_shm_pool_resize
 * on top, which on MINIX means the IPC server growing the pool underneath.
 *
 * So that the result can be checked without looking at a screen, every frame's
 * pixels are checksummed and printed; the compositor prints the checksum of
 * what it received.  If those agree, the pixels really did cross the process
 * boundary through shared memory, intact.
 */

#include <sys/types.h>
#include <sys/mman.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "wlr-layer-shell-client-protocol.h"

#define W0	240			/* the size we pick when told "you choose" */
#define H0	160

static struct wl_display	*display;
static struct wl_compositor	*compositor;
static struct wl_shm		*shm;
static struct xdg_wm_base	*wm_base;
static struct wl_seat		*seat;
static struct wl_subcompositor	*subcompositor;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct wl_pointer	*pointer;
static struct wl_surface	*surface;
static struct xdg_surface	*xdg_surface;

/* The pool outlives any one buffer: a resize grows it in place. */
static struct wl_shm_pool	*pool;
static int			 pool_fd = -1;
static size_t			 pool_size;
static void			*pool_data;

static struct wl_buffer		*buffer;
static int			 cur_w, cur_h;		/* what we last drew */
static int			 want_w, want_h;	/* what we were told to be */

static int	 configured;
static int	 released;
static int	 got_keymap;
static int	 frames;
static uint32_t	 last_checksum;

/* ------------------------------------------------------------- registry */

static void
registry_global(void *data, struct wl_registry *reg, uint32_t name,
    const char *iface, uint32_t version)
{
	(void)data; (void)version;

	if (strcmp(iface, "wl_compositor") == 0)
		compositor = wl_registry_bind(reg, name,
		    &wl_compositor_interface, 4);
	else if (strcmp(iface, "wl_shm") == 0)
		shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
	else if (strcmp(iface, "xdg_wm_base") == 0)
		wm_base = wl_registry_bind(reg, name, &xdg_wm_base_interface, 1);
	else if (strcmp(iface, "wl_seat") == 0)
		seat = wl_registry_bind(reg, name, &wl_seat_interface, 5);
	else if (strcmp(iface, "wl_subcompositor") == 0)
		subcompositor = wl_registry_bind(reg, name,
		    &wl_subcompositor_interface, 1);
	else if (strcmp(iface, "zwlr_layer_shell_v1") == 0)
		layer_shell = wl_registry_bind(reg, name,
		    &zwlr_layer_shell_v1_interface, 1);

	printf("  global: %s v%u\n", iface, version);
}

static void
registry_global_remove(void *data, struct wl_registry *reg, uint32_t name)
{
	(void)data; (void)reg; (void)name;
}

static const struct wl_registry_listener registry_listener = {
	.global		= registry_global,
	.global_remove	= registry_global_remove,
};

/* ----------------------------------------------------------- xdg_wm_base */

static void
wm_base_ping(void *data, struct xdg_wm_base *b, uint32_t serial)
{
	(void)data;
	xdg_wm_base_pong(b, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
	.ping = wm_base_ping,
};

/* ---------------------------------------------------------- xdg_surface */

static void
xdg_surface_configure(void *data, struct xdg_surface *xs, uint32_t serial)
{
	(void)data;
	xdg_surface_ack_configure(xs, serial);
	configured = 1;
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_configure,
};

/*
 * The compositor telling us how big to be.  Zero means "you choose", which is
 * what the first configure carries; anything else we must adopt, and the frame
 * we draw next has to be that size.
 */
static void
toplevel_configure(void *data, struct xdg_toplevel *t, int32_t w, int32_t h,
    struct wl_array *states)
{
	(void)data; (void)t; (void)states;

	want_w = (w > 0) ? w : W0;
	want_h = (h > 0) ? h : H0;
}

static void
toplevel_close(void *data, struct xdg_toplevel *t)
{
	(void)data; (void)t;
	printf("wlclient: compositor asked us to close\n");
	exit(0);
}

static const struct xdg_toplevel_listener toplevel_listener = {
	.configure	= toplevel_configure,
	.close		= toplevel_close,
};

/* ------------------------------------------------------------- wl_buffer */

static void
buffer_release(void *data, struct wl_buffer *b)
{
	(void)data; (void)b;
	released = 1;
}

static const struct wl_buffer_listener buffer_listener = {
	.release = buffer_release,
};

/* ------------------------------------------------------------ wl_keyboard */

static void
kbd_keymap(void *data, struct wl_keyboard *k, uint32_t format, int32_t fd,
    uint32_t size)
{
	char *text;

	(void)data; (void)k;

	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || fd < 0) {
		printf("  keymap: none (format %u)\n", format);
		if (fd >= 0)
			close(fd);
		return;
	}

	text = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (text == MAP_FAILED) {
		printf("  keymap: mmap failed: %s\n", strerror(errno));
		close(fd);
		return;
	}

	got_keymap = (size > 32 && strstr(text, "xkb_keymap") != NULL);
	printf("  keymap: %u bytes, %s\n", size,
	    got_keymap ? "looks like XKB text" : "NOT XKB TEXT");

	munmap(text, size);
	close(fd);
}

static void kbd_enter(void *d, struct wl_keyboard *k, uint32_t s,
    struct wl_surface *surf, struct wl_array *keys)
{ (void)d; (void)k; (void)s; (void)surf; (void)keys; }
static void kbd_leave(void *d, struct wl_keyboard *k, uint32_t s,
    struct wl_surface *surf)
{ (void)d; (void)k; (void)s; (void)surf; }
static void kbd_key(void *d, struct wl_keyboard *k, uint32_t s, uint32_t t,
    uint32_t key, uint32_t state)
{
	(void)d; (void)k; (void)s; (void)t;
	printf("  key: evdev %u %s\n", key, state ? "pressed" : "released");
	fflush(stdout);
}
static void kbd_modifiers(void *d, struct wl_keyboard *k, uint32_t s,
    uint32_t dep, uint32_t lat, uint32_t lock, uint32_t grp)
{ (void)d; (void)k; (void)s; (void)dep; (void)lat; (void)lock; (void)grp; }
static void kbd_repeat_info(void *d, struct wl_keyboard *k, int32_t rate,
    int32_t delay)
{ (void)d; (void)k; (void)rate; (void)delay; }

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap		= kbd_keymap,
	.enter		= kbd_enter,
	.leave		= kbd_leave,
	.key		= kbd_key,
	.modifiers	= kbd_modifiers,
	.repeat_info	= kbd_repeat_info,
};

/* ------------------------------------------------------------------ pool */

/*
 * shm_open, unlink the name at once, keep the fd: the idiom every Wayland
 * client uses, and the one that made the IPC server's lookup have to learn that
 * an object outlives its name.
 */
static int
pool_create(size_t size)
{
	static const char *name = "/wlclient-pool";
	int fd;

	(void)shm_unlink(name);
	if ((fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600)) < 0) {
		fprintf(stderr, "shm_open: %s\n", strerror(errno));
		return -1;
	}
	(void)shm_unlink(name);

	if (ftruncate(fd, size) != 0) {
		fprintf(stderr, "ftruncate: %s\n", strerror(errno));
		close(fd);
		return -1;
	}

	pool_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (pool_data == MAP_FAILED) {
		fprintf(stderr, "mmap: %s\n", strerror(errno));
		close(fd);
		return -1;
	}

	pool_fd = fd;
	pool_size = size;
	pool = wl_shm_create_pool(shm, fd, (int32_t)size);
	return 0;
}

/*
 * Grow the pool to hold a bigger frame.  On MINIX this is the interesting one:
 * ftruncate() then a fresh mmap() of the same fd makes the IPC server grow the
 * region behind it, and wl_shm_pool_resize makes the compositor re-map its end.
 */
static int
pool_grow(size_t size)
{
	void *nd;

	if (ftruncate(pool_fd, size) != 0) {
		fprintf(stderr, "ftruncate(grow): %s\n", strerror(errno));
		return -1;
	}

	nd = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, pool_fd, 0);
	if (nd == MAP_FAILED) {
		fprintf(stderr, "mmap(grow): %s\n", strerror(errno));
		return -1;
	}

	(void)munmap(pool_data, pool_size);
	pool_data = nd;
	pool_size = size;

	wl_shm_pool_resize(pool, (int32_t)size);
	printf("wlclient: pool grown to %zu bytes\n", size);
	return 0;
}

/* A pattern that is obviously right or obviously wrong: border + diagonal. */
static uint32_t
paint(uint32_t *px, int w, int h)
{
	uint32_t sum = 0;
	int x, y;

	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			uint32_t c;

			if (x < 2 || y < 2 || x >= w - 2 || y >= h - 2)
				c = 0x00ff6600;		/* orange border */
			else if ((x * h) / w == y)
				c = 0x00ffffff;		/* white diagonal */
			else
				c = 0x00224466;		/* blue field */

			px[y * w + x] = c;
			sum = sum * 31u + c;
		}
	}
	return sum;
}

/* Draw one frame at (w, h) and commit it. */
static int
draw_frame(int w, int h)
{
	size_t need = (size_t)w * 4 * h;

	if (pool_fd < 0) {
		if (pool_create(need) != 0)
			return -1;
	} else if (need > pool_size) {
		if (pool_grow(need) != 0)
			return -1;
	}

	last_checksum = paint(pool_data, w, h);

	if (buffer != NULL)
		wl_buffer_destroy(buffer);
	buffer = wl_shm_pool_create_buffer(pool, 0, w, h, w * 4,
	    WL_SHM_FORMAT_XRGB8888);
	wl_buffer_add_listener(buffer, &buffer_listener, NULL);

	released = 0;
	wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_damage(surface, 0, 0, w, h);
	wl_surface_commit(surface);
	wl_display_flush(display);

	cur_w = w;
	cur_h = h;
	frames++;

	printf("wlclient: frame %d %dx%d checksum=0x%08x\n", frames, w, h,
	    last_checksum);
	fflush(stdout);
	return 0;
}

/* ------------------------------------- popups, subsurfaces, cursors ---- */

static struct wl_surface	*sub_surface;
static struct wl_subsurface	*subsurface;

static struct wl_surface	*popup_surface;
static struct xdg_surface	*popup_xdg;
static struct xdg_popup		*popup;
static int			 popup_done;
static int			 popup_configured;

static struct wl_surface	*cursor_surface;
static int			 cursor_set;

/*
 * A self-contained buffer: its own pool, filled with one colour.  A real client
 * would sub-allocate one pool; for a test this is clearer and exercises the
 * same shm path once per surface.
 */
static struct wl_buffer *
solid_buffer(int w, int h, uint32_t colour)
{
	static int seq;
	char name[32];
	size_t size = (size_t)w * 4 * h;
	struct wl_shm_pool *p;
	struct wl_buffer *b;
	uint32_t *px;
	void *data;
	int fd, i;

	snprintf(name, sizeof(name), "/wlclient-aux%d", seq++);
	(void)shm_unlink(name);
	if ((fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600)) < 0)
		return NULL;
	(void)shm_unlink(name);

	if (ftruncate(fd, size) != 0) {
		close(fd);
		return NULL;
	}
	data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		close(fd);
		return NULL;
	}

	px = data;
	for (i = 0; i < w * h; i++)
		px[i] = colour;

	p = wl_shm_create_pool(shm, fd, (int32_t)size);
	b = wl_shm_pool_create_buffer(p, 0, w, h, w * 4,
	    WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(p);
	close(fd);
	(void)munmap(data, size);
	return b;
}

static void
popup_configure(void *data, struct xdg_popup *pp, int32_t x, int32_t y,
    int32_t w, int32_t h)
{
	(void)data; (void)pp;
	printf("wlclient: popup configured at %+d%+d %dx%d\n", x, y, w, h);
	popup_configured = 1;
}

static void
popup_done_cb(void *data, struct xdg_popup *pp)
{
	(void)data; (void)pp;
	printf("wlclient: popup dismissed by the compositor\n");
	popup_done = 1;
}

static const struct xdg_popup_listener popup_listener = {
	.configure	= popup_configure,
	.popup_done	= popup_done_cb,
};

static void
popup_xdg_configure(void *data, struct xdg_surface *xs, uint32_t serial)
{
	(void)data;
	xdg_surface_ack_configure(xs, serial);
}

static const struct xdg_surface_listener popup_xdg_listener = {
	.configure = popup_xdg_configure,
};

/* ---- pointer: set our own cursor on enter ----------------------------- */

static void
ptr_enter(void *data, struct wl_pointer *p, uint32_t serial,
    struct wl_surface *surf, wl_fixed_t sx, wl_fixed_t sy)
{
	struct wl_buffer *b;

	(void)data; (void)surf; (void)sx; (void)sy;

	if (cursor_surface == NULL || cursor_set)
		return;

	/* A client cursor: a small opaque square with a hotspot at its corner. */
	if ((b = solid_buffer(16, 16, 0xff00ff00)) != NULL) {
		wl_surface_attach(cursor_surface, b, 0, 0);
		wl_surface_damage(cursor_surface, 0, 0, 16, 16);
		wl_surface_commit(cursor_surface);
	}
	wl_pointer_set_cursor(p, serial, cursor_surface, 2, 2);
	cursor_set = 1;
	printf("wlclient: set a client cursor (16x16, hotspot 2,2)\n");
}

static void ptr_leave(void *d, struct wl_pointer *p, uint32_t s,
    struct wl_surface *surf) { (void)d; (void)p; (void)s; (void)surf; }
static void ptr_motion(void *d, struct wl_pointer *p, uint32_t t,
    wl_fixed_t x, wl_fixed_t y) { (void)d; (void)p; (void)t; (void)x; (void)y; }
static void ptr_button(void *d, struct wl_pointer *p, uint32_t s, uint32_t t,
    uint32_t b, uint32_t st) { (void)d; (void)p; (void)s; (void)t; (void)b; (void)st; }
static void ptr_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t a,
    wl_fixed_t v) { (void)d; (void)p; (void)t; (void)a; (void)v; }
static void ptr_frame(void *d, struct wl_pointer *p) { (void)d; (void)p; }
static void ptr_axis_src(void *d, struct wl_pointer *p, uint32_t s)
{ (void)d; (void)p; (void)s; }
static void ptr_axis_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a)
{ (void)d; (void)p; (void)t; (void)a; }
static void ptr_axis_disc(void *d, struct wl_pointer *p, uint32_t a, int32_t v)
{ (void)d; (void)p; (void)a; (void)v; }

static const struct wl_pointer_listener pointer_listener = {
	.enter			= ptr_enter,
	.leave			= ptr_leave,
	.motion			= ptr_motion,
	.button			= ptr_button,
	.axis			= ptr_axis,
	.frame			= ptr_frame,
	.axis_source		= ptr_axis_src,
	.axis_stop		= ptr_axis_stop,
	.axis_discrete		= ptr_axis_disc,
};

/* Build a subsurface inside the window, and a popup hanging off it. */
static void
make_subsurface(void)
{
	struct wl_buffer *b;

	if (subcompositor == NULL) {
		printf("wlclient: no wl_subcompositor\n");
		return;
	}

	sub_surface = wl_compositor_create_surface(compositor);
	subsurface = wl_subcompositor_get_subsurface(subcompositor, sub_surface,
	    surface);
	wl_subsurface_set_position(subsurface, 24, 24);
	wl_subsurface_set_desync(subsurface);

	if ((b = solid_buffer(64, 48, 0xffcc3366)) != NULL) {
		wl_surface_attach(sub_surface, b, 0, 0);
		wl_surface_damage(sub_surface, 0, 0, 64, 48);
		wl_surface_commit(sub_surface);
		wl_surface_commit(surface);	/* subsurfaces apply with parent */
		printf("wlclient: subsurface 64x48 at +24+24\n");
	}
}

static void
make_popup(void)
{
	struct xdg_positioner *pos;
	struct wl_buffer *b;

	pos = xdg_wm_base_create_positioner(wm_base);
	xdg_positioner_set_size(pos, 120, 80);
	xdg_positioner_set_anchor_rect(pos, 10, 10, 40, 20);
	xdg_positioner_set_anchor(pos, XDG_POSITIONER_ANCHOR_BOTTOM_LEFT);
	xdg_positioner_set_gravity(pos, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);

	popup_surface = wl_compositor_create_surface(compositor);
	popup_xdg = xdg_wm_base_get_xdg_surface(wm_base, popup_surface);
	xdg_surface_add_listener(popup_xdg, &popup_xdg_listener, NULL);

	popup = xdg_surface_get_popup(popup_xdg, xdg_surface, pos);
	xdg_popup_add_listener(popup, &popup_listener, NULL);
	xdg_positioner_destroy(pos);

	wl_surface_commit(popup_surface);	/* ask to be configured */
	wl_display_roundtrip(display);

	if ((b = solid_buffer(120, 80, 0xff33aa55)) != NULL) {
		wl_surface_attach(popup_surface, b, 0, 0);
		wl_surface_damage(popup_surface, 0, 0, 120, 80);
		wl_surface_commit(popup_surface);
		printf("wlclient: popup 120x80 committed\n");
	}
}


/* ------------------------------------------------- panel (layer-shell) --- */

/*
 * What a panel does: take a layer surface, anchor it to an edge, let the
 * compositor work out the size, and reserve an exclusive strip so windows do
 * not sit underneath.  This is how lxqt-panel (and every other Wayland panel)
 * docks itself, so it is worth proving on its own.
 */
static int panel_configured;
static int panel_w, panel_h;

static void
layer_configure(void *data, struct zwlr_layer_surface_v1 *ls, uint32_t serial,
    uint32_t w, uint32_t h)
{
    (void)data;
    zwlr_layer_surface_v1_ack_configure(ls, serial);
    panel_w = (int)w;
    panel_h = (int)h;
    panel_configured = 1;
    printf("wlpanel: configured %ux%u\n", w, h);
    fflush(stdout);
}

static void
layer_closed(void *data, struct zwlr_layer_surface_v1 *ls)
{
    (void)data; (void)ls;
    printf("wlpanel: closed by the compositor\n");
    exit(0);
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
    .configure = layer_configure,
    .closed    = layer_closed,
};

static int
run_panel(int seconds)
{
    struct zwlr_layer_surface_v1 *ls;
    struct wl_surface *psurf;
    struct wl_buffer *b;
    time_t deadline;
    int spins;

    if (layer_shell == NULL) {
        fprintf(stderr, "wlpanel: no zwlr_layer_shell_v1\n");
        return 1;
    }

    psurf = wl_compositor_create_surface(compositor);
    ls = zwlr_layer_shell_v1_get_layer_surface(layer_shell, psurf, NULL,
             ZWLR_LAYER_SHELL_V1_LAYER_TOP, "wlpanel");
    zwlr_layer_surface_v1_add_listener(ls, &layer_listener, NULL);

    /*
     * Anchored left+right+bottom: spanning the width is the compositor's job,
     * so the width is left at zero and only the height is named.  A panel that
     * had to know the screen width could not be written portably at all.
     */
    zwlr_layer_surface_v1_set_size(ls, 0, 28);
    zwlr_layer_surface_v1_set_anchor(ls,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM);
    zwlr_layer_surface_v1_set_exclusive_zone(ls, 28);
    zwlr_layer_surface_v1_set_keyboard_interactivity(ls,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);

    wl_surface_commit(psurf);   /* ask to be configured */

    spins = 0;
    while (!panel_configured && spins++ < 200)
        if (wl_display_dispatch(display) < 0)
            break;

    if (!panel_configured) {
        printf("wlpanel: NOT configured\n");
        return 1;
    }

    if ((b = solid_buffer(panel_w, panel_h, 0xff204060)) == NULL) {
        fprintf(stderr, "wlpanel: buffer failed\n");
        return 1;
    }
    wl_surface_attach(psurf, b, 0, 0);
    wl_surface_damage(psurf, 0, 0, panel_w, panel_h);
    wl_surface_commit(psurf);
    wl_display_flush(display);

    printf("wlpanel: committed %dx%d\n", panel_w, panel_h);

    /*
     * roundtrip, not dispatch: a static panel asks for nothing, so no events
     * ever arrive and wl_display_dispatch() would simply block forever.  A
     * roundtrip always completes -- it sends a sync and waits for its reply.
     */
    deadline = time(NULL) + (seconds > 0 ? seconds : 5);
    while (time(NULL) < deadline) {
        if (wl_display_roundtrip(display) < 0)
            break;
        sleep(1);
    }

    printf("wlpanel: %s\n", panel_w > 0 && panel_h == 28 ? "ALL PASS"
                                                          : "FAILURES PRESENT");
    return (panel_w > 0 && panel_h == 28) ? 0 : 1;
}

/* ------------------------------------------------------------------ main */


int
main(int argc, char **argv)
{
	struct wl_registry *registry;
	struct xdg_toplevel *toplevel;
	struct wl_keyboard *kbd = NULL;
	int seconds = (argc > 1) ? atoi(argv[1]) : 0;
	time_t deadline;
	int spins;

	setvbuf(stdout, NULL, _IONBF, 0);
	printf("wlclient: connecting\n");

	if ((display = wl_display_connect(NULL)) == NULL) {
		fprintf(stderr, "wl_display_connect: %s (is wlcompd running, "
		    "and WAYLAND_DISPLAY/XDG_RUNTIME_DIR set?)\n",
		    strerror(errno));
		return 1;
	}
	printf("wlclient: connected\n");

	registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);

	/* Panel mode: a layer surface instead of a toplevel. */
	if (argc > 2 && strcmp(argv[2], "panel") == 0) {
		int rc = run_panel(seconds);

		wl_display_disconnect(display);
		return rc;
	}

	if (compositor == NULL || shm == NULL || wm_base == NULL) {
		fprintf(stderr, "missing globals\n");
		return 1;
	}
	xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);

	if (seat != NULL) {
		kbd = wl_seat_get_keyboard(seat);
		if (kbd != NULL)
			wl_keyboard_add_listener(kbd, &keyboard_listener, NULL);

		pointer = wl_seat_get_pointer(seat);
		if (pointer != NULL)
			wl_pointer_add_listener(pointer, &pointer_listener,
			    NULL);
	}

	surface = wl_compositor_create_surface(compositor);
	xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, surface);
	xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);
	toplevel = xdg_surface_get_toplevel(xdg_surface);
	xdg_toplevel_add_listener(toplevel, &toplevel_listener, NULL);
	xdg_toplevel_set_title(toplevel, "wlclient");
	wl_surface_commit(surface);		/* ask to be configured */

	/* xdg_shell forbids drawing before the first configure. */
	spins = 0;
	while (!configured && spins++ < 200)
		if (wl_display_dispatch(display) < 0)
			break;

	if (!configured) {
		printf("wlclient: NOT configured (giving up)\n");
		return 1;
	}
	printf("wlclient: configured, told %dx%d\n", want_w, want_h);

	if (draw_frame(want_w, want_h) != 0)
		return 1;

	/* The compositor releases the buffer once it has taken the frame. */
	spins = 0;
	while (!released && spins++ < 200)
		if (wl_display_dispatch(display) < 0)
			break;

	printf("wlclient: buffer %s\n",
	    released ? "released by the compositor" : "NOT released");
	printf("wlclient: keymap %s\n",
	    kbd == NULL ? "not requested"
	    : got_keymap ? "received and valid" : "MISSING/INVALID");

	/*
	 * The pieces a real toolkit needs: a subsurface (Qt and GTK both use
	 * them), a popup (every menu is one), and a client-supplied cursor.
	 */
	if (compositor != NULL)
		cursor_surface = wl_compositor_create_surface(compositor);

	make_subsurface();
	make_popup();
	wl_display_roundtrip(display);

	/*
	 * Stay up for a while, honouring configures.  This is where an
	 * interactive resize lands: the compositor sends a new size, we adopt it
	 * and redraw at that size -- growing the shm pool if the frame no longer
	 * fits.
	 */
	if (seconds > 0) {
		printf("wlclient: serving configures for %ds\n", seconds);
		deadline = time(NULL) + seconds;

		while (time(NULL) < deadline) {
			if (wl_display_dispatch(display) < 0)
				break;

			if (want_w != cur_w || want_h != cur_h) {
				printf("wlclient: reconfigured %dx%d -> %dx%d\n",
				    cur_w, cur_h, want_w, want_h);
				if (draw_frame(want_w, want_h) != 0)
					break;
			}
		}
		printf("wlclient: served %d frame(s), final size %dx%d\n",
		    frames, cur_w, cur_h);
	}

	printf("wlclient: %s\n",
	    (frames > 0 && (kbd == NULL || got_keymap)) ? "ALL PASS"
							: "FAILURES PRESENT");

	wl_display_disconnect(display);
	return (frames > 0 && (kbd == NULL || got_keymap)) ? 0 : 1;
}
