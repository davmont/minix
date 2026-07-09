/* fbclient.h - tiny client-side helper for the MINIX PoC compositor. */
#ifndef FBCLIENT_H
#define FBCLIENT_H

#include <stdint.h>

typedef struct {
	int		fd;		/* socket to the compositor */
	int		shmid;		/* SysV shm segment id */
	uint32_t       *pixels;		/* attached surface, w*h x8r8g8b8 */
	int		w, h;
	int		x, y;		/* last position told by CONFIGURE */
} fbc_win_t;

/* Connect to the compositor; returns socket fd or -1. */
int  fbc_connect(void);

/* Create a window of w*h: allocates shm, attaches it, registers with the
 * compositor.  Draw into win->pixels, then fbc_commit().  Returns 0/-1. */
int  fbc_create_window(int fd, int w, int h, const char *title, fbc_win_t *win);

/* Push a damaged sub-rectangle (x,y,w,h in surface coords) to the screen. */
void fbc_commit(fbc_win_t *win, int x, int y, int w, int h);

/* Non-blocking: if an event is pending, fills *out and returns 1, else 0.
 * out->type is FBC_MOUSE (x,y,buttons) or FBC_CLOSED. */
struct fbc_msg;
int  fbc_poll(fbc_win_t *win, struct fbc_msg *out);

void fbc_destroy(fbc_win_t *win);

#endif /* FBCLIENT_H */
