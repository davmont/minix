/* client_clock.c - a live-updating window client for the MINIX PoC
 * compositor.  Each second it advances a filling bar and shifts the panel
 * hue, then commits - proving an independent process drives its own window
 * and only its surface is recomposited. */
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
		uint32_t bg = 0x00101820 + ((uint32_t)((t * 7) & 0x3f) << 16);
		int fill = 20 + (t % 20) * ((W - 40) / 20);
		box(win.pixels, W, 0, 0, W, H, bg);		/* panel */
		box(win.pixels, W, 20, 30, W - 40, 30, 0x00303840); /* trough */
		box(win.pixels, W, 20, 30, fill, 30, 0x0040d080);   /* fill */
		/* a row of second-ticks */
		int k;
		for (k = 0; k < (t % 20); k++)
			box(win.pixels, W, 20 + k * 14, 80, 10, 30, 0x00f0c040);
		fbc_commit(&win, 0, 0, W, H);

		struct fbc_msg ev;
		while (fbc_poll(&win, &ev))
			if (ev.type == FBC_CLOSED) goto done;
		sleep(1);
	}
done:
	fbc_destroy(&win);
	return 0;
}
