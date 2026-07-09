/* fbcompd.c - a minimal framebuffer compositor for MINIX.
 *
 * Owns /dev/fb0 (via libfbgui) and serves window clients over an AF_UNIX
 * socket.  Each client draws x8r8g8b8 pixels into a SysV shm segment and
 * commits; the compositor attaches the segment, composites all windows
 * (with a title bar + title) plus a cursor into the libfbgui back buffer,
 * and presents.  Left-drag on a title bar moves a window; a click in a
 * window body is forwarded to that client.
 *
 * Proof of concept, not a service: single back buffer, no resize, no
 * keyboard routing.  See README.md.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pixman.h>
#include "fbgui.h"
#include "fbcomp_proto.h"

#define MAXCLI		16
#define TITLEH		22
#define CURW		12
#define CURH		19

struct win {
	int		used;
	int		fd;		/* owning client, -1 if none */
	int		shmid;
	uint32_t       *pix;
	pixman_image_t *img;
	int		w, h;
	int		x, y;		/* title-bar top-left on screen */
	char		title[FBCOMP_TITLE_MAX];
};

static fbgui_t *G;
static int	cli[MAXCLI];		/* connected client fds, -1 = free */
static struct win wins[MAXCLI];		/* index order = stacking, high = top */
static int	nwin;
static int	cx, cy;
static FILE    *lg;

static const pixman_color_t c_desk  = { 0x1000, 0x2000, 0x3800, 0xffff };
static const pixman_color_t c_tbar  = { 0x2000, 0x4000, 0xd000, 0xffff };
static const pixman_color_t c_ttext = { 0xffff, 0xffff, 0xffff, 0xffff };
static const pixman_color_t c_cur   = { 0xf000, 0xf000, 0xf000, 0xffff };

static void
recomposite(void)
{
	pixman_image_t *back = fbgui_surface(G);
	int i;

	fbgui_fill_rect(G, &c_desk, 0, 0, fbgui_width(G), fbgui_height(G));
	for (i = 0; i < nwin; i++) {
		struct win *w = &wins[i];
		if (!w->used) continue;
		fbgui_fill_rect(G, &c_tbar, w->x, w->y, w->w, TITLEH);
		fbgui_draw_text(G, w->x + 8, w->y + 16, w->title, &c_ttext, 14);
		pixman_image_composite32(PIXMAN_OP_SRC, w->img, NULL, back,
		    0, 0, 0, 0, w->x, w->y + TITLEH, w->w, w->h);
	}
	fbgui_fill_rect(G, &c_cur, cx, cy, CURW, CURH);
}

static void
present_scene(void)
{
	fbgui_damage_all(G);
	fbgui_present(G);
}

static struct win *
win_at(int sx, int sy, int *topbar)
{
	int i;
	for (i = nwin - 1; i >= 0; i--) {
		struct win *w = &wins[i];
		if (!w->used) continue;
		if (sx >= w->x && sx < w->x + w->w &&
		    sy >= w->y && sy < w->y + w->h + TITLEH) {
			*topbar = (sy < w->y + TITLEH);
			return w;
		}
	}
	return NULL;
}

static int
win_index(const struct win *w) { return (int)(w - wins); }

static void
raise_win(int idx)
{
	struct win tmp;
	int i;
	if (idx < 0 || idx >= nwin || idx == nwin - 1) return;
	tmp = wins[idx];
	for (i = idx; i < nwin - 1; i++) wins[i] = wins[i + 1];
	wins[nwin - 1] = tmp;
}

static struct win *
win_by_fd(int fd)
{
	int i;
	for (i = 0; i < nwin; i++)
		if (wins[i].used && wins[i].fd == fd) return &wins[i];
	return NULL;
}

static void
add_window(int fd, const struct fbc_msg *m)
{
	struct win *w;
	void *p;

	if (nwin >= MAXCLI || m->w <= 0 || m->h <= 0 ||
	    m->w > 4096 || m->h > 4096) {
		fprintf(lg, "reject window %dx%d\n", m->w, m->h);
		return;
	}
	if ((p = shmat(m->shmid, NULL, SHM_RDONLY)) == (void *)-1) {
		fprintf(lg, "shmat(%d): %s\n", m->shmid, strerror(errno));
		return;
	}
	w = &wins[nwin];
	memset(w, 0, sizeof *w);
	w->used = 1; w->fd = fd; w->shmid = m->shmid;
	w->pix = (uint32_t *)p; w->w = m->w; w->h = m->h;
	w->x = 60 + nwin * 48; w->y = 60 + nwin * 40;
	strlcpy(w->title, m->title, sizeof w->title);
	w->img = pixman_image_create_bits(PIXMAN_x8r8g8b8, w->w, w->h,
	    w->pix, w->w * 4);
	nwin++;
	fprintf(lg, "window '%s' %dx%d shmid=%d at %d,%d nwin=%d\n",
	    w->title, w->w, w->h, w->shmid, w->x, w->y, nwin);
}

static void
drop_window_by_fd(int fd)
{
	struct win *w = win_by_fd(fd);
	int idx, i;
	if (w == NULL) return;
	idx = win_index(w);
	if (w->img) pixman_image_unref(w->img);
	if (w->pix) (void)shmdt(w->pix);
	for (i = idx; i < nwin - 1; i++) wins[i] = wins[i + 1];
	nwin--;
}

static void
drop_client(int slot)
{
	int fd = cli[slot];
	if (fd < 0) return;
	drop_window_by_fd(fd);
	close(fd);
	cli[slot] = -1;
	fprintf(lg, "client fd=%d gone\n", fd);
}

int
main(void)
{
	struct sockaddr_un sa;
	int lfd, mfd, i, dragging = -1, btn = 0;
	fd_set rf;

	lg = fopen("/tmp/COMP", "w");
	if (lg == NULL) lg = stdout;
	setvbuf(lg, NULL, _IONBF, 0);
	for (i = 0; i < MAXCLI; i++) cli[i] = -1;

	if ((G = fbgui_open()) == NULL) {
		fprintf(lg, "fbgui_open failed (no /dev/fb0?)\n"); return 1;
	}
	if (fbgui_load_font(G, "/mnt/font.ttf") != 0)
		fprintf(lg, "warning: no font\n");
	cx = fbgui_width(G) / 2; cy = fbgui_height(G) / 2;

	unlink(FBCOMP_SOCK);
	if ((lfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0 ||
	    (memset(&sa, 0, sizeof sa), sa.sun_family = AF_UNIX,
	     strlcpy(sa.sun_path, FBCOMP_SOCK, sizeof sa.sun_path),
	     bind(lfd, (struct sockaddr *)&sa, sizeof sa)) < 0 ||
	    listen(lfd, 8) < 0) {
		fprintf(lg, "socket setup: %s\n", strerror(errno)); return 1;
	}
	mfd = fbgui_open_mouse();

	recomposite();
	fbgui_present_full(G);
	fprintf(lg, "COMPOSITOR READY fb=%dx%d sock=%s\n",
	    fbgui_width(G), fbgui_height(G), FBCOMP_SOCK);

	for (;;) {
		struct timeval tv = { 0, 30000 };
		int maxfd = lfd;

		FD_ZERO(&rf);
		FD_SET(lfd, &rf);
		if (mfd >= 0) { FD_SET(mfd, &rf); if (mfd > maxfd) maxfd = mfd; }
		for (i = 0; i < MAXCLI; i++)
			if (cli[i] >= 0) {
				FD_SET(cli[i], &rf);
				if (cli[i] > maxfd) maxfd = cli[i];
			}

		if (select(maxfd + 1, &rf, NULL, NULL, &tv) < 0) {
			if (errno == EINTR) continue;
			break;
		}

		if (FD_ISSET(lfd, &rf)) {
			int cfd = accept(lfd, NULL, NULL);
			if (cfd >= 0) {
				for (i = 0; i < MAXCLI && cli[i] >= 0; i++) ;
				if (i < MAXCLI) { cli[i] = cfd;
				    fprintf(lg, "client fd=%d\n", cfd); }
				else close(cfd);
			}
		}

		for (i = 0; i < MAXCLI; i++) {
			struct fbc_msg m;
			int fd = cli[i], r;
			if (fd < 0 || !FD_ISSET(fd, &rf)) continue;
			r = (int)read(fd, &m, sizeof m);
			if (r != (int)sizeof m) { drop_client(i);
			    recomposite(); present_scene(); continue; }
			switch (m.type) {
			case FBC_CREATE_WINDOW:
				add_window(fd, &m);
				recomposite(); present_scene(); break;
			case FBC_COMMIT:
				recomposite(); present_scene(); break;
			case FBC_DESTROY:
				drop_client(i);
				recomposite(); present_scene(); break;
			}
		}

		if (mfd >= 0 && FD_ISSET(mfd, &rf)) {
			int dx=0, dy=0, ncx, ncy, topbar, changed=0;
			struct win *w;

			fbgui_read_mouse(mfd, &dx, &dy, &btn);
			ncx = cx + dx; ncy = cy + dy;
			if (ncx < 0) ncx = 0;
			if (ncy < 0) ncy = 0;
			if (ncx > fbgui_width(G)-2) ncx = fbgui_width(G)-2;
			if (ncy > fbgui_height(G)-2) ncy = fbgui_height(G)-2;

			if (btn & 1) {
				if (dragging < 0) {
					w = win_at(ncx, ncy, &topbar);
					fprintf(lg, "btn-down at %d,%d -> %s topbar=%d\n",
					    ncx, ncy, w?w->title:"(none)", w?topbar:0);
					if (w != NULL) {
						raise_win(win_index(w));
						if (topbar) { dragging = nwin-1;
						    fprintf(lg,"GRAB drag '%s'\n",wins[nwin-1].title); }
						else {
						    struct fbc_msg em;
						    struct win *tw=&wins[nwin-1];
						    memset(&em,0,sizeof em);
						    em.type=FBC_MOUSE;
						    em.x=ncx-tw->x;
						    em.y=ncy-tw->y-TITLEH;
						    em.buttons=btn;
						    (void)write(tw->fd,&em,sizeof em);
						}
						changed = 1;
					}
				} else if (dragging >= 0 && dragging < nwin) {
					struct fbc_msg cm;
					wins[dragging].x += ncx - cx;
					wins[dragging].y += ncy - cy;
					fprintf(lg, "DRAG '%s' to %d,%d\n",
					    wins[dragging].title,
					    wins[dragging].x, wins[dragging].y);
					memset(&cm, 0, sizeof cm);
					cm.type = FBC_CONFIGURE;
					cm.x = wins[dragging].x;
					cm.y = wins[dragging].y;
					(void)write(wins[dragging].fd, &cm,
					    sizeof cm);
					changed = 1;
				}
			} else {
				dragging = -1;
			}
			if (ncx != cx || ncy != cy || changed) {
				cx = ncx; cy = ncy;
				recomposite(); present_scene();
			}
		}
	}
	return 0;
}
