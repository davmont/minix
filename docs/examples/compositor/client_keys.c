/* client_keys.c - a keyboard client for the MINIX PoC compositor.
 * The compositor grabs /dev/kbdmux and routes key events to the focused
 * (topmost) window as FBC_KEY messages; raise this window (click it) and it
 * collects typed characters.  Each key is echoed to stdout ("keys: ...") and
 * drawn as a growing row of coloured bars - a base has no client-side font. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fbcomp_proto.h>
#include <fbclient.h>

#define W 380
#define H 170

static char text[128];
static int  tlen;

static void
box(uint32_t *px, int stride, int x, int y, int w, int h, uint32_t c)
{
	int i, j;
	for (j = y; j < y + h; j++)
		for (i = x; i < x + w; i++)
			px[j * stride + i] = c;
}

/* Draw the panel plus one coloured bar per typed character. */
static void
redraw(fbc_win_t *win)
{
	int w = win->w, h = win->h, i, bw;
	box(win->pixels, w, 0, 0, w, h, 0x00202a38);		/* panel bg */
	box(win->pixels, w, 0, 0, w, 6, 0x0040d080);		/* top accent */
	bw = 10;
	for (i = 0; i < tlen && 12 + i * (bw + 2) + bw < w; i++) {
		uint32_t c = 0x00404040 | ((uint32_t)(text[i] * 7) << 16)
		    | ((uint32_t)(text[i] * 5) << 8);
		box(win->pixels, w, 12 + i * (bw + 2), 20, bw, h - 40, c);
	}
	fbc_commit(win, 0, 0, w, h);
}

int
main(void)
{
	fbc_win_t win;
	int fd;

	setvbuf(stdout, NULL, _IONBF, 0);	/* echo lands in logs immediately */
	if ((fd = fbc_connect()) < 0) { printf("keys: no compositor\n"); return 1; }
	if (fbc_create_window(fd, W, H, "Keys", &win) < 0) {
		printf("keys: create_window failed\n"); return 1;
	}
	redraw(&win);
	printf("keys: window up - click me, then type\n");

	for (;;) {
		struct fbc_msg ev;
		int rw = 0, rh = 0, dirty = 0, closed = 0;

		while (fbc_poll(&win, &ev)) {
			if (ev.type == FBC_CLOSED) { closed = 1; break; }
			if (ev.type == FBC_CONFIGURE) { rw = ev.w; rh = ev.h; }
			if (ev.type == FBC_KEY && ev.w /* press */) {
				char c = (char)ev.y;
				if (c == '\b') {
					if (tlen > 0) tlen--;
					dirty = 1;
				} else if (c >= ' ' && tlen < (int)sizeof text - 1) {
					text[tlen++] = c;
					dirty = 1;
				}
				if (dirty) {
					text[tlen] = '\0';
					printf("keys: %s\n", text);
				}
			}
		}
		if (closed)
			break;
		if (rw > 0 && (rw != win.w || rh != win.h)) {
			if (fbc_resize(&win, rw, rh) == 0)
				dirty = 1;
		}
		if (dirty)
			redraw(&win);
		else
			usleep(20000);
	}
	fbc_destroy(&win);
	return 0;
}
