/*	$NetBSD: poll.c,v 1.3 2008/04/29 05:46:08 martin Exp $	*/

/*-
 * Copyright (c) 2003 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles Blundell.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/poll.h>
#include <errno.h>
#include <fcntl.h>

int
poll(struct pollfd *p, nfds_t nfds, int timout)
{
	fd_set rd, wr, except;
	struct timeval tv, *tvp;
	nfds_t i;
	int highfd, rval, nval;

	/*
	 * select cannot tell us much wrt POLL*BAND, POLLPRI or POLLHUP.
	 * Descriptors that are not open get POLLNVAL, as POSIX has it, rather
	 * than failing the whole call: select() fails with EBADF, and only
	 * then each descriptor is checked.
	 */
	for (i = 0; i < nfds; i++)
		p[i].revents = 0;

	for (;;) {
		FD_ZERO(&rd);
		FD_ZERO(&wr);
		FD_ZERO(&except);

		highfd = -1;
		nval = 0;
		for (i = 0; i < nfds; i++) {
			if (p[i].fd < 0)
				continue;
			if (p[i].fd >= FD_SETSIZE)
				p[i].revents = POLLNVAL;	/* cannot be open */
			if (p[i].revents & POLLNVAL) {
				nval++;
				continue;
			}
			if (p[i].fd > highfd)
				highfd = p[i].fd;

			if (p[i].events & (POLLIN|POLLRDNORM))
				FD_SET(p[i].fd, &rd);
			if (p[i].events & (POLLOUT|POLLWRNORM|POLLWRBAND))
				FD_SET(p[i].fd, &wr);
			if (p[i].events & (POLLRDBAND|POLLPRI))
				FD_SET(p[i].fd, &except);
		}

		/* With a POLLNVAL to report, do not wait. */
		if (nval > 0 || timout == 0) {
			tv.tv_sec = tv.tv_usec = 0;
			tvp = &tv;
		} else if (timout < 0) {
			tvp = NULL;
		} else {
			tv.tv_sec = timout / 1000;
			tv.tv_usec = (timout % 1000) * 1000;
			tvp = &tv;
		}

		rval = select(highfd + 1, &rd, &wr, &except, tvp);
		if (rval >= 0)
			break;
		if (errno != EBADF)
			return -1;

		/* Find the descriptors that are not open.  If all are, the
		 * error is select's own: pass it on rather than loop.
		 */
		rval = nval;
		for (i = 0; i < nfds; i++)
			if (p[i].fd >= 0 && !(p[i].revents & POLLNVAL) &&
			    fcntl(p[i].fd, F_GETFD) == -1 && errno == EBADF) {
				p[i].revents = POLLNVAL;
				rval++;
			}
		if (rval == nval) {
			errno = EBADF;
			return -1;
		}
	}

	rval = 0;
	for (i = 0; i < nfds; i++) {
		if (p[i].fd < 0)
			continue;
		if (!(p[i].revents & POLLNVAL)) {
			if (FD_ISSET(p[i].fd, &rd))
				p[i].revents |=
				    p[i].events & (POLLIN|POLLRDNORM);
			if (FD_ISSET(p[i].fd, &wr))
				p[i].revents |=
				    p[i].events & (POLLOUT|POLLWRNORM|POLLWRBAND);
			if (FD_ISSET(p[i].fd, &except))
				p[i].revents |=
				    p[i].events & (POLLRDBAND|POLLPRI);
		}
		if (p[i].revents != 0)
			rval++;
	}
	return rval;
}
