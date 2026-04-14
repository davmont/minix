/* $NetBSD: t_dup2.c,v 1.0 2024/05/23 00:00:00 jules Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jukka Ruohonen.
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
__RCSID("$NetBSD: t_dup2.c,v 1.0 2024/05/23 00:00:00 jules Exp $");

#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sysexits.h>

static char	path[] = "dup2_test";

static void
check_mode(void)
{
	int mode[3] = { O_RDONLY, O_WRONLY, O_RDWR   };
	int perm[5] = { 0700, 0400, 0600, 0444, 0666 };
	struct stat st, st1;
	int fd, fd1, fd2;
	size_t i, j;

	/*
	 * Check that a duplicated descriptor
	 * retains the mode of the original file.
	 */
	for (i = 0; i < __arraycount(mode); i++) {

		for (j = 0; j < __arraycount(perm); j++) {

			fd1 = open(path, mode[i] | O_CREAT, perm[j]);
			fd2 = open("/etc/passwd", O_RDONLY);

			ATF_REQUIRE(fd1 >= 0);
			ATF_REQUIRE(fd2 >= 0);

			fd = dup2(fd1, fd2);

			ATF_REQUIRE(fd >= 0);

			(void)memset(&st, 0, sizeof(struct stat));
			(void)memset(&st1, 0, sizeof(struct stat));

			ATF_REQUIRE(fstat(fd, &st) == 0);
			ATF_REQUIRE(fstat(fd1, &st1) == 0);

			if (st.st_mode != st1.st_mode)
				atf_tc_fail("invalid mode");

			(void)close(fd);
			(void)close(fd1);
			/* fd2 was closed by dup2 if it wasn't equal to fd */
			(void)unlink(path);
		}
	}
}

ATF_TC(dup2_basic);
ATF_TC_HEAD(dup2_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of dup2(2)");
}

ATF_TC_BODY(dup2_basic, tc)
{
	int fd, fd1, fd2;

	fd1 = open("/etc/passwd", O_RDONLY);
	fd2 = open("/etc/passwd", O_RDONLY);

	ATF_REQUIRE(fd1 >= 0);
	ATF_REQUIRE(fd2 >= 0);

	fd = dup2(fd1, fd2);
	ATF_REQUIRE(fd >= 0);

	if (fd != fd2)
		atf_tc_fail("invalid descriptor");

	(void)close(fd);
	(void)close(fd1);

	/* fd2 should have been closed by dup2 */
	ATF_REQUIRE(close(fd2) != 0);
}

ATF_TC(dup2_err);
ATF_TC_HEAD(dup2_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test error conditions of dup2(2)");
}

ATF_TC_BODY(dup2_err, tc)
{
	int fd;

	fd = open("/etc/passwd", O_RDONLY);
	ATF_REQUIRE(fd >= 0);

	errno = 0;
	ATF_REQUIRE_ERRNO(EBADF, dup2(-1, -1) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(EBADF, dup2(fd, -1) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(EBADF, dup2(-1, fd) == -1);

	/*
	 * Note that this should not fail with EINVAL.
	 */
	ATF_REQUIRE(dup2(fd, fd) != -1);

	(void)close(fd);
}

ATF_TC(dup2_max);
ATF_TC_HEAD(dup2_max, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test dup2(2) against limits");
}

ATF_TC_BODY(dup2_max, tc)
{
	struct rlimit res;

	(void)memset(&res, 0, sizeof(struct rlimit));
	(void)getrlimit(RLIMIT_NOFILE, &res);

	errno = 0;
	ATF_REQUIRE_ERRNO(EBADF, dup2(STDERR_FILENO, res.rlim_cur + 1) == -1);
}

ATF_TC_WITH_CLEANUP(dup2_mode);
ATF_TC_HEAD(dup2_mode, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of dup2(2) mode retention");
}

ATF_TC_BODY(dup2_mode, tc)
{
	check_mode();
}

ATF_TC_CLEANUP(dup2_mode, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(dup2_read);
ATF_TC_HEAD(dup2_read, tc)
{
	atf_tc_set_md_var(tc, "descr", "Verify reading from a dup2'd fd works");
}

ATF_TC_BODY(dup2_read, tc)
{
	int fd1, fd2, fd3;
	char buf[16];
	const char *str = "dup2_test_data";
	ssize_t n;

	fd1 = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);
	ATF_REQUIRE(fd1 >= 0);

	n = write(fd1, str, strlen(str));
	ATF_REQUIRE(n == (ssize_t)strlen(str));

	ATF_REQUIRE(lseek(fd1, 0, SEEK_SET) == 0);

	fd2 = open("/etc/passwd", O_RDONLY);
	ATF_REQUIRE(fd2 >= 0);

	fd3 = dup2(fd1, fd2);
	ATF_REQUIRE(fd3 == fd2);

	(void)memset(buf, 0, sizeof(buf));
	n = read(fd3, buf, sizeof(buf));
	ATF_REQUIRE(n == (ssize_t)strlen(str));
	ATF_REQUIRE(memcmp(buf, str, strlen(str)) == 0);

	(void)close(fd1);
	(void)close(fd3);
}

ATF_TC_CLEANUP(dup2_read, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(dup2_cloexec);
ATF_TC_HEAD(dup2_cloexec, tc)
{
	atf_tc_set_md_var(tc, "descr", "Verify dup2 clears FD_CLOEXEC");
}

ATF_TC_BODY(dup2_cloexec, tc)
{
	int fd1, fd2, flags;

	fd1 = open(path, O_RDWR | O_CREAT, 0666);
	ATF_REQUIRE(fd1 >= 0);

	fd2 = open("/etc/passwd", O_RDONLY);
	ATF_REQUIRE(fd2 >= 0);

	/* Set FD_CLOEXEC on both */
	ATF_REQUIRE(fcntl(fd1, F_SETFD, FD_CLOEXEC) == 0);
	ATF_REQUIRE(fcntl(fd2, F_SETFD, FD_CLOEXEC) == 0);

	ATF_REQUIRE(dup2(fd1, fd2) == fd2);

	flags = fcntl(fd2, F_GETFD);
	ATF_REQUIRE(flags != -1);
	ATF_REQUIRE((flags & FD_CLOEXEC) == 0);

	(void)close(fd1);
	(void)close(fd2);
}

ATF_TC_CLEANUP(dup2_cloexec, tc)
{
	(void)unlink(path);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, dup2_basic);
	ATF_TP_ADD_TC(tp, dup2_err);
	ATF_TP_ADD_TC(tp, dup2_max);
	ATF_TP_ADD_TC(tp, dup2_mode);
	ATF_TP_ADD_TC(tp, dup2_read);
	ATF_TP_ADD_TC(tp, dup2_cloexec);

	return atf_no_error();
}
