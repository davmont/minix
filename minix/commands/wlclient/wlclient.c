/*	wlclient - a Wayland client, and the end-to-end proof of the stack
 *
 * Connects to wlcompd, binds the globals, makes an xdg_toplevel, draws into a
 * wl_shm buffer and commits it.  That one sequence exercises every piece this
 * branch added, in the arrangement they were built for:
 *
 *   shm_open + MAP_SHARED for the pool, its fd passed to the compositor over
 *   SCM_RIGHTS (wl_shm), requests marshalled and dispatched through libffi, the
 *   compositor's event loop running on the poll(2) emulation, and the keymap it
 *   sends compiled by libxkbcommon.
 *
 * It paints a recognisable pattern -- a border and a diagonal -- and, so that
 * the result can be checked without looking at a screen, prints the exact
 * checksum of the pixels it committed.  The compositor prints the checksum of
 * what it received.  If those two agree, a client's pixels really did cross the
 * process boundary through shared memory and arrive intact.
 *
 * Exits 0 once the compositor has released the buffer -- i.e. once it has taken
 * the frame.
 */

#include <sys/types.h>
#include <sys/mman.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

#define W	240
#define H	160
#define STRIDE	(W * 4)
#define SIZE	(STRIDE * H)

static struct wl_compositor	*compositor;
static struct wl_shm		*shm;
static struct xdg_wm_base	*wm_base;
static struct wl_seat		*seat;

static int	 configured;
static int	 released;
static int	 got_keymap;
static uint32_t	 checksum;

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

static void
toplevel_configure(void *data, struct xdg_toplevel *t, int32_t w, int32_t h,
    struct wl_array *states)
{
	(void)data; (void)t; (void)w; (void)h; (void)states;
}

static void
toplevel_close(void *data, struct xdg_toplevel *t)
{
	(void)data; (void)t;
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

/*
 * We do not need the keymap to draw, but taking it proves the last link: the
 * compositor sends one as an fd, and this is the call every real toolkit makes
 * on it.
 */
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

	/* It must actually be a keymap, not a page of zeroes. */
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

/* ------------------------------------------------------------------ main */

/*
 * The pool: shm_open, size it, map it writable and MAP_SHARED, and hand the fd
 * to the compositor.  This is wlprobe's chain, now driven by libwayland.
 */
static int
make_pool(void **datap)
{
	static const char *name = "/wlclient-pool";
	void *data;
	int fd;

	(void)shm_unlink(name);
	if ((fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600)) < 0) {
		fprintf(stderr, "shm_open: %s\n", strerror(errno));
		return -1;
	}
	(void)shm_unlink(name);		/* the fd is enough from here on */

	if (ftruncate(fd, SIZE) != 0) {
		fprintf(stderr, "ftruncate: %s\n", strerror(errno));
		close(fd);
		return -1;
	}

	data = mmap(NULL, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		fprintf(stderr, "mmap: %s\n", strerror(errno));
		close(fd);
		return -1;
	}

	*datap = data;
	return fd;
}

/* A pattern that is obviously right or obviously wrong: border + diagonal. */
static void
paint(uint32_t *px)
{
	int x, y;

	checksum = 0;
	for (y = 0; y < H; y++) {
		for (x = 0; x < W; x++) {
			uint32_t c;

			if (x < 2 || y < 2 || x >= W - 2 || y >= H - 2)
				c = 0x00ff6600;		/* orange border */
			else if ((x * H) / W == y)
				c = 0x00ffffff;		/* white diagonal */
			else
				c = 0x00224466;		/* blue field */

			px[y * W + x] = c;
			checksum = checksum * 31u + c;
		}
	}
}

int
main(void)
{
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *toplevel;
	struct wl_shm_pool *pool;
	struct wl_buffer *buffer;
	struct wl_keyboard *kbd = NULL;
	uint32_t *px;
	void *data;
	int fd, spins;

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
	wl_display_roundtrip(display);		/* globals arrive here */

	if (compositor == NULL || shm == NULL || wm_base == NULL) {
		fprintf(stderr, "missing globals (compositor=%p shm=%p "
		    "wm_base=%p)\n", (void *)compositor, (void *)shm,
		    (void *)wm_base);
		return 1;
	}
	xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);

	if (seat != NULL) {
		kbd = wl_seat_get_keyboard(seat);
		if (kbd != NULL)
			wl_keyboard_add_listener(kbd, &keyboard_listener, NULL);
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
	while (!configured && spins++ < 200) {
		if (wl_display_dispatch(display) < 0)
			break;
	}
	printf("wlclient: %s\n",
	    configured ? "configured" : "NOT configured (giving up)");
	if (!configured)
		return 1;

	if ((fd = make_pool(&data)) < 0)
		return 1;
	px = data;
	paint(px);
	printf("wlclient: painted %dx%d, checksum=0x%08x\n", W, H, checksum);

	/* Hand the pool's fd over: SCM_RIGHTS, underneath. */
	pool = wl_shm_create_pool(shm, fd, SIZE);
	buffer = wl_shm_pool_create_buffer(pool, 0, W, H, STRIDE,
	    WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);

	wl_buffer_add_listener(buffer, &buffer_listener, NULL);

	wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_damage(surface, 0, 0, W, H);
	wl_surface_commit(surface);
	wl_display_flush(display);

	/* The compositor releases the buffer once it has taken the frame. */
	spins = 0;
	while (!released && spins++ < 200) {
		if (wl_display_dispatch(display) < 0)
			break;
	}

	printf("wlclient: buffer %s\n",
	    released ? "released by the compositor" : "NOT released");
	printf("wlclient: keymap %s\n",
	    kbd == NULL ? "not requested"
	    : got_keymap ? "received and valid" : "MISSING/INVALID");

	printf("wlclient: %s\n",
	    (released && (kbd == NULL || got_keymap)) ? "ALL PASS"
						      : "FAILURES PRESENT");

	wl_display_disconnect(display);
	return (released && (kbd == NULL || got_keymap)) ? 0 : 1;
}
