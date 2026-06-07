/*
 * MINIX compatibility shim: NetBSD renamed libblacklist's <blacklist.h> and its
 * blacklist_* API to <blocklist.h> / blocklist_*.  MINIX still ships the older
 * <blacklist.h>, so map the names BIND 9.18 expects onto the ones MINIX provides.
 * This header is on BIND's -I path, so it satisfies <blocklist.h> includes.
 */
#ifndef _MINIX_BIND_BLOCKLIST_COMPAT_H_
#define _MINIX_BIND_BLOCKLIST_COMPAT_H_

#include <blacklist.h>

#define blocklist	blacklist
#define blocklist_open	blacklist_open
#define blocklist_close	blacklist_close
#define blocklist_r	blacklist_r
#define blocklist_sa	blacklist_sa
#define blocklist_sa_r	blacklist_sa_r

#endif /* _MINIX_BIND_BLOCKLIST_COMPAT_H_ */
