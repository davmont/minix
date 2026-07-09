/* client_hello.c - a static window client for the MINIX PoC compositor.
 * Draws a solid panel with a few coloured blocks into its shm surface. */
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

int
main(void)
{
	fbc_win_t win;
	int fd;

	if ((fd = fbc_connect()) < 0) { printf("hello: no compositor\n"); return 1; }
	if (fbc_create_window(fd, W, H, "Hello", &win) < 0) {
		printf("hello: create_window failed\n"); return 1;
	}
	box(win.pixels, W, 0, 0, W, H, 0x00203040);		/* panel bg */
	box(win.pixels, W, 20, 24, 120, 60, 0x00d05030);	/* orange */
	box(win.pixels, W, 160, 24, 120, 60, 0x0030b060);	/* green */
	box(win.pixels, W, 20, 100, 260, 56, 0x00c0c0d0);	/* light bar */
	fbc_commit(&win, 0, 0, W, H);
	printf("hello: window up (%dx%d)\n", W, H);

	/* Idle: just stay alive so the window persists; drain events. */
	for (;;) {
		struct fbc_msg ev;
		if (fbc_poll(&win, &ev) && ev.type == FBC_CLOSED) break;
		sleep(1);
	}
	fbc_destroy(&win);
	return 0;
}
