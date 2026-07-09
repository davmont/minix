/* MINIX micro-GUI PoC, built on libfbgui.
 *
 * A desktop with two draggable windows (FreeType titles/body), an arrow
 * cursor driven by /dev/mouse relative motion, and title-bar drag.  Uses
 * libfbgui for the framebuffer, pixman surface, text and mouse input.
 *
 * Performance note: the whole scene is recomposited into the off-screen
 * back buffer on every change (cheap - RAM), but only the changed
 * rectangles are pushed to /dev/fb0 via fbgui_present() (the expensive
 * part is the framebuffer device I/O, so minimising it removes the lag).
 * Each frame logs its bytes-written; compare to a full frame (W*H*4).
 */
#include <sys/select.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <fbgui.h>

static fbgui_t *G;
static int W, H;
static FILE *lg;

/* ---- 12x19 arrow cursor, X=opaque ---- */
#define CURW 12
#define CURH 19
static const char *arrow[CURH] = {
"X...........","XX..........","X.X.........","X..X........","X...X.......",
"X....X......","X.....X.....","X......X....","X.......X...","X........X..",
"X.....XXXXX.","X..X..X.....","X.X.X.X.....",".XX..X.X....",".X...X.X....",
"....X...X...","....X...X...",".....X.X....",".....XXX...."};

typedef struct { int x, y, w, h; pixman_color_t title, body;
    const char *name, *l1, *l2; } Win;
static Win wins[2];
static int order[2] = { 0, 1 };		/* order[1] = front-most */

#define WINBORDER 2
#define TITLEH 28

static void
draw_cursor(int cx, int cy)
{
	pixman_color_t wht = { 0xffff, 0xffff, 0xffff, 0xffff };
	int r, c;

	for (r = 0; r < CURH; r++)
		for (c = 0; c < CURW; c++)
			if (arrow[r][c] == 'X')
				fbgui_fill_rect(G, &wht, cx + c, cy + r, 1, 1);
}

/* Recomposite the entire scene into the back buffer. */
static void
compose(int cx, int cy)
{
	pixman_color_t desk = { 0x1010, 0x2020, 0x3838, 0xffff };
	pixman_color_t wht = { 0xffff, 0xffff, 0xffff, 0xffff };
	pixman_color_t dark = { 0x1818, 0x1818, 0x2020, 0xffff };
	pixman_color_t bord = { 0x8080, 0x8080, 0x9090, 0xffff };
	int i;

	fbgui_fill_rect(G, &desk, 0, 0, W, H);
	for (i = 0; i < 2; i++) {
		Win *w = &wins[order[i]];
		fbgui_fill_rect(G, &bord, w->x - WINBORDER, w->y - WINBORDER,
		    w->w + 2 * WINBORDER, w->h + 2 * WINBORDER);
		fbgui_fill_rect(G, &w->body, w->x, w->y, w->w, w->h);
		fbgui_fill_rect(G, &w->title, w->x, w->y, w->w, TITLEH);
		fbgui_draw_text(G, w->x + 10, w->y + 20, w->name, &wht, 18);
		fbgui_draw_text(G, w->x + 12, w->y + 58, w->l1, &wht, 16);
		fbgui_draw_text(G, w->x + 12, w->y + 82, w->l2, &dark, 16);
	}
	draw_cursor(cx, cy);
}

/* Damage the bounding box of a window including its border. */
static void
damage_win(const Win *w)
{
	fbgui_damage(G, w->x - WINBORDER, w->y - WINBORDER,
	    w->w + 2 * WINBORDER, w->h + 2 * WINBORDER);
}

static int
in_title(const Win *w, int x, int y)
{
	return x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + TITLEH;
}

int
main(void)
{
	int cx, cy, drag = -1, buttons = 0, obuttons = 0;
	int mfd;
	struct timeval start, now;
	int evtotal = 0;
	long total_bytes = 0, frames = 0;

	lg = fopen("/tmp/GUI", "w");
	if (lg == NULL)
		lg = stdout;
	setvbuf(lg, NULL, _IONBF, 0);

	if ((G = fbgui_open()) == NULL) {
		fprintf(lg, "fbgui_open failed (no /dev/fb0?)\n");
		return 1;
	}
	W = fbgui_width(G);
	H = fbgui_height(G);
	fprintf(lg, "fb %dx%d full-frame=%d bytes\n", W, H, W * H * 4);

	if (fbgui_load_font(G, "/mnt/font.ttf") != 0) {
		fprintf(lg, "font load failed\n");
		return 1;
	}

	{
		pixman_color_t tblue = { 0x2020, 0x4040, 0xd0d0, 0xffff };
		pixman_color_t bwhite = { 0xf0f0, 0xf0f0, 0xf8f8, 0xffff };
		pixman_color_t tgreen = { 0x2020, 0xa0a0, 0x3030, 0xffff };
		pixman_color_t bgrey = { 0xd8d8, 0xdcdc, 0xe0e0, 0xffff };
		wins[0] = (Win){ 200, 150, 360, 180, tblue, bwhite, "Terminal",
		    "MINIX 3 amd64 graphics stack", "libfbgui: pixman + FreeType" };
		wins[1] = (Win){ 430, 260, 340, 170, tgreen, bgrey, "About",
		    "No X11, no display server.", "Damage-tracked framebuffer." };
	}

	cx = W / 2;
	cy = H / 2;
	mfd = fbgui_open_mouse();
	fprintf(lg, "mouse fd=%d\n", mfd);

	compose(cx, cy);
	total_bytes += fbgui_present_full(G);
	frames++;
	fprintf(lg, "MICROGUI READY cursor=%d,%d\n", cx, cy);

	gettimeofday(&start, NULL);
	for (;;) {
		fd_set rf;
		struct timeval tv = { 0, 40000 };
		int ocx = cx, ocy = cy, owx = 0, owy = 0, changed = 0;
		int dx = 0, dy = 0, n;

		gettimeofday(&now, NULL);
		if (now.tv_sec - start.tv_sec > 75)
			break;

		FD_ZERO(&rf);
		if (mfd >= 0)
			FD_SET(mfd, &rf);
		n = (mfd >= 0) ? select(mfd + 1, &rf, NULL, NULL, &tv)
		    : (usleep(40000), 0);
		if (n <= 0 || !(mfd >= 0 && FD_ISSET(mfd, &rf)))
			continue;

		evtotal += fbgui_read_mouse(mfd, &dx, &dy, &buttons);
		if (drag >= 0) {
			owx = wins[drag].x;
			owy = wins[drag].y;
		}

		/* Cursor motion. */
		if (dx != 0 || dy != 0) {
			cx += dx;
			cy += dy;
			if (cx < 0) cx = 0;
			if (cx >= W) cx = W - 1;
			if (cy < 0) cy = 0;
			if (cy >= H) cy = H - 1;
			changed = 1;
		}

		/* Button press: grab a title bar under the cursor and raise. */
		if ((buttons & 1) && !(obuttons & 1)) {
			int i;
			for (i = 1; i >= 0; i--) {
				Win *w = &wins[order[i]];
				if (in_title(w, cx, cy)) {
					drag = order[i];
					order[i] = order[1];
					order[1] = drag;
					damage_win(&wins[0]);
					damage_win(&wins[1]);
					fprintf(lg, "grab win %d (%s)\n",
					    drag, w->name);
					changed = 1;
					break;
				}
			}
		} else if (!(buttons & 1) && (obuttons & 1)) {
			if (drag >= 0)
				fprintf(lg, "drop win %d\n", drag);
			drag = -1;
		}
		obuttons = buttons;

		/* Drag: move the window and damage old+new footprints. */
		if (drag >= 0 && (cx != ocx || cy != ocy)) {
			wins[drag].x += cx - ocx;
			wins[drag].y += cy - ocy;
			fbgui_damage(G, owx - WINBORDER, owy - WINBORDER,
			    wins[drag].w + 2 * WINBORDER,
			    wins[drag].h + 2 * WINBORDER);
			damage_win(&wins[drag]);
			changed = 1;
		}

		/* Cursor damage: old and new positions. */
		if (cx != ocx || cy != ocy) {
			fbgui_damage(G, ocx, ocy, CURW, CURH);
			fbgui_damage(G, cx, cy, CURW, CURH);
		}

		if (changed) {
			long b;
			compose(cx, cy);
			b = fbgui_present(G);
			total_bytes += b;
			frames++;
			if (frames <= 60)
				fprintf(lg, "frame %ld: %ld bytes (cur=%d,%d "
				    "drag=%d)\n", frames, b, cx, cy, drag);
		}
	}

	fprintf(lg, "MICROGUI DONE events=%d frames=%ld total_bytes=%ld "
	    "avg=%ld\n", evtotal, frames, total_bytes,
	    frames ? total_bytes / frames : 0);
	fprintf(lg, "  (a single full frame would be %d bytes)\n", W * H * 4);
	fprintf(lg, "MICROGUI PASS\n");
	fbgui_close(G);
	return 0;
}
