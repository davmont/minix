/* client_clock.c - a live-updating window client for the MINIX PoC
 * compositor.  Each second it advances a filling bar and shifts the panel
 * hue, then commits - proving an independent process drives its own window
 * and only its surface is recomposited.  It also repaints at the new size
 * when the compositor resizes it (drag the bottom-right grip). */
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <fbcomp_proto.h>
#include <fbclient.h>

#define W 320
#define H 140

static void
box(uint32_t *px, int stride, int x, int y, int w, int h, uint32_t c)
{
	int i, j;
	for (j = y; j < y + h; j++)
		for (i = x; i < x + w; i++)
			px[j * stride + i] = c;
}

/* Paint one frame at tick t using the window's current size. */
static void
draw(fbc_win_t *win, int t)
{
	int w = win->w, h = win->h, k;
	uint32_t bg = 0x00101820 + ((uint32_t)((t * 7) & 0x3f) << 16);
	int fill = 20 + (t % 20) * ((w - 40) / 20);
	box(win->pixels, w, 0, 0, w, h, bg);			/* panel */
	box(win->pixels, w, 20, 30, w - 40, 30, 0x00303840);	/* trough */
	box(win->pixels, w, 20, 30, fill, 30, 0x0040d080);	/* fill */
	for (k = 0; k < (t % 20) && 20 + k * 14 + 10 < w; k++)
		box(win->pixels, w, 20 + k * 14, 80, 10, 30, 0x00f0c040);
	fbc_commit(win, 0, 0, w, h);
}

int
main(void)
{
	fbc_win_t win;
	int fd, t;

	if ((fd = fbc_connect()) < 0) { printf("clock: no compositor\n"); return 1; }
	if (fbc_create_window(fd, W, H, "Clock", &win) < 0) {
		printf("clock: create_window failed\n"); return 1;
	}
	printf("clock: window up (%dx%d)\n", W, H);

	for (t = 0; t < 90; t++) {
		struct fbc_msg ev;
		draw(&win, t);
		while (fbc_poll(&win, &ev)) {
			if (ev.type == FBC_CLOSED)
				goto done;
			if (ev.type == FBC_CONFIGURE &&
			    (ev.w != win.w || ev.h != win.h)) {
				if (fbc_resize(&win, ev.w, ev.h) == 0)
					draw(&win, t);
			}
		}
		sleep(1);
	}
done:
	fbc_destroy(&win);
	return 0;
}
