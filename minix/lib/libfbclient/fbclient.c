/* fbclient.c - client-side helper for the MINIX PoC compositor. */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "fbcomp_proto.h"
#include "fbclient.h"

int
fbc_connect(void)
{
	struct sockaddr_un sa;
	int fd;

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		return -1;
	memset(&sa, 0, sizeof sa);
	sa.sun_family = AF_UNIX;
	strlcpy(sa.sun_path, FBCOMP_SOCK, sizeof sa.sun_path);
	if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

int
fbc_create_window(int fd, int w, int h, const char *title, fbc_win_t *win)
{
	struct fbc_msg m;
	size_t bytes = (size_t)w * h * 4;
	int id;
	void *p;

	if ((id = shmget(IPC_PRIVATE, bytes, IPC_CREAT | 0600)) < 0)
		return -1;
	if ((p = shmat(id, NULL, 0)) == (void *)-1)
		return -1;
	memset(p, 0, bytes);

	memset(&m, 0, sizeof m);
	m.type = FBC_CREATE_WINDOW;
	m.w = w; m.h = h; m.shmid = id;
	if (title != NULL)
		strlcpy(m.title, title, sizeof m.title);
	if (write(fd, &m, sizeof m) != (ssize_t)sizeof m)
		return -1;

	win->fd = fd; win->shmid = id; win->pixels = p;
	win->w = w; win->h = h; win->x = 0; win->y = 0;
	return 0;
}

void
fbc_commit(fbc_win_t *win, int x, int y, int w, int h)
{
	struct fbc_msg m;

	memset(&m, 0, sizeof m);
	m.type = FBC_COMMIT;
	m.x = x; m.y = y; m.w = w; m.h = h;
	(void)write(win->fd, &m, sizeof m);
}

int
fbc_resize(fbc_win_t *win, int w, int h)
{
	struct fbc_msg m;
	size_t bytes = (size_t)w * h * 4;
	int id;
	void *p;

	if (w <= 0 || h <= 0)
		return -1;
	if ((id = shmget(IPC_PRIVATE, bytes, IPC_CREAT | 0600)) < 0)
		return -1;
	if ((p = shmat(id, NULL, 0)) == (void *)-1) {
		(void)shmctl(id, IPC_RMID, NULL);
		return -1;
	}
	memset(p, 0, bytes);

	/* Release the old surface.  IPC_RMID only frees it once the
	 * compositor also detaches, so it stays valid until then. */
	if (win->pixels != NULL)
		(void)shmdt(win->pixels);
	(void)shmctl(win->shmid, IPC_RMID, NULL);

	win->shmid = id; win->pixels = (uint32_t *)p; win->w = w; win->h = h;

	memset(&m, 0, sizeof m);
	m.type = FBC_SET_SURFACE;
	m.shmid = id; m.w = w; m.h = h;
	if (write(win->fd, &m, sizeof m) != (ssize_t)sizeof m)
		return -1;
	return 0;
}

int
fbc_poll(fbc_win_t *win, struct fbc_msg *out)
{
	int fl, r;

	fl = fcntl(win->fd, F_GETFL, 0);
	(void)fcntl(win->fd, F_SETFL, fl | O_NONBLOCK);
	r = (int)read(win->fd, out, sizeof *out);
	(void)fcntl(win->fd, F_SETFL, fl);
	if (r == (int)sizeof *out) {
		if (out->type == FBC_CONFIGURE) {
			win->x = out->x; win->y = out->y;
		}
		return 1;
	}
	return 0;
}

void
fbc_destroy(fbc_win_t *win)
{
	struct fbc_msg m;

	memset(&m, 0, sizeof m);
	m.type = FBC_DESTROY;
	(void)write(win->fd, &m, sizeof m);
	if (win->pixels != NULL)
		(void)shmdt(win->pixels);
	(void)shmctl(win->shmid, IPC_RMID, NULL);
	close(win->fd);
}
