/* client_hello.c - a simple window client for the MINIX PoC compositor.
 * Draws a solid panel with a few coloured blocks into its shm surface and
 * repaints whenever the compositor resizes the window (drag the grip). */
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <fbcomp_proto.h>
#include <fbclient.h>

#define W 300
#define H 180

static void
box(uint32_t *px, int stride, int x, int y, int w, int h, uint32_t c)
{
	int i, j;
	for (j = y; j < y + h; j++)
		for (i = x; i < x + w; i++)
			px[j * stride + i] = c;
}

/* Paint the whole surface using the window's current size as the stride. */
static void
redraw(fbc_win_t *win)
{
	int w = win->w, h = win->h;
	box(win->pixels, w, 0, 0, w, h, 0x00203040);		/* panel bg */
	box(win->pixels, w, 20, 24, w/2 - 30, h/3, 0x00d05030);	/* orange */
	box(win->pixels, w, w/2 + 10, 24, w/2 - 30, h/3, 0x0030b060); /* green */
	box(win->pixels, w, 20, h/2 + 10, w - 40, h/3 - 12, 0x00c0c0d0); /* bar */
	fbc_commit(win, 0, 0, w, h);
}

int
main(void)
{
	fbc_win_t win;
	int fd;

	if ((fd = fbc_connect()) < 0) { printf("hello: no compositor\n"); return 1; }
	if (fbc_create_window(fd, W, H, "Hello", &win) < 0) {
		printf("hello: create_window failed\n"); return 1;
	}
	redraw(&win);
	printf("hello: window up (%dx%d)\n", W, H);

	/* Stay alive so the window persists; repaint on resize, quit on close. */
	for (;;) {
		struct fbc_msg ev;
		if (fbc_poll(&win, &ev)) {
			if (ev.type == FBC_CLOSED)
				break;
			if (ev.type == FBC_CONFIGURE &&
			    (ev.w != win.w || ev.h != win.h)) {
				if (fbc_resize(&win, ev.w, ev.h) == 0)
					redraw(&win);
			}
			continue;
		}
		sleep(1);
	}
	fbc_destroy(&win);
	return 0;
}
