# $NetBSD: Makefile,v 1.2 2013/11/09 00:26:06 christos Exp $

USE_SHLIBDIR=yes
.include <bsd.own.mk>

LIB=event

#__MINIX: Unsupported: kqueue.c
SRCS= \
buffer.c \
bufferevent.c \
bufferevent_filter.c \
bufferevent_pair.c \
bufferevent_ratelim.c \
bufferevent_sock.c \
evdns.c \
event.c \
event_tagging.c \
evmap.c \
evrpc.c \
evthread.c \
evutil.c \
evutil_rand.c \
evutil_time.c \
http.c \
listener.c \
log.c \
poll.c \
select.c \
signal.c

.if defined(HAVE_GCC) && ${HAVE_GCC} == 48
.if ${MACHINE_ARCH} == "sh3el"
COPTS.evdns.c=	-O0
.endif
.endif

.if defined(__MINIX)
# MINIX has no libpthread, so libevent is built with thread support
# disabled (EVENT__DISABLE_THREAD_SUPPORT).  BEV_LOCK()/BEV_UNLOCK() and
# the other locking helpers then expand to no-ops, leaving the variables
# that only existed to be locked unused.
CWARNFLAGS+=	-Wno-error=unused-variable
.endif

.include <bsd.lib.mk>
