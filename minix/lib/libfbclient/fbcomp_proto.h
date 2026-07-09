/* fbcomp_proto.h - wire protocol between fbcompd and its clients.
 *
 * MINIX PoC compositor.  One fixed-size tagged struct is read/written whole
 * in both directions, so there is no partial-message framing to worry about.
 * Surface pixels are NOT sent over the socket: a client allocates a SysV
 * shared-memory segment (shmget IPC_PRIVATE), draws x8r8g8b8 pixels into it,
 * and passes the shmid (a plain int) in CREATE_WINDOW; the compositor
 * attaches the same segment read-only (classic MIT-SHM).
 */
#ifndef FBCOMP_PROTO_H
#define FBCOMP_PROTO_H

#include <stdint.h>

#define FBCOMP_SOCK	"/tmp/fbcompd.sock"
#define FBCOMP_TITLE_MAX 48

enum {
	/* client -> server */
	FBC_CREATE_WINDOW = 1,	/* w,h,shmid,title -> a new window */
	FBC_COMMIT,		/* x,y,w,h = damaged sub-rect of the surface */
	FBC_DESTROY,
	FBC_SET_SURFACE,	/* shmid,w,h = replacement surface after a resize */
	/* server -> client */
	FBC_CONFIGURE = 100,	/* x,y = position; w,h = current size (client
				 * reallocates its surface if w,h changed) */
	FBC_MOUSE,		/* x,y = pointer in window coords, buttons bitmap */
	FBC_CLOSED
};

struct fbc_msg {
	uint32_t type;
	int32_t	 x, y, w, h;
	int32_t	 shmid;
	int32_t	 buttons;
	char	 title[FBCOMP_TITLE_MAX];
};

#endif /* FBCOMP_PROTO_H */
