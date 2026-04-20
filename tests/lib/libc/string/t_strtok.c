/*	$NetBSD: t_strtok.c,v 1.1 2024/05/11 00:00:00 jules Exp $	*/

/*-
 * Copyright (c) 2024 The NetBSD Foundation, Inc.
 * All rights reserved.
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
__RCSID("$NetBSD: t_strtok.c,v 1.1 2024/05/11 00:00:00 jules Exp $");

#include <atf-c.h>
#include <string.h>

ATF_TC(strtok_basic);
ATF_TC_HEAD(strtok_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test strtok(3) with basic parsing");
}

ATF_TC_BODY(strtok_basic, tc)
{
	char str[] = "hello world test";
	char *p;

	p = strtok(str, " ");
	ATF_CHECK_STREQ(p, "hello");
	p = strtok(NULL, " ");
	ATF_CHECK_STREQ(p, "world");
	p = strtok(NULL, " ");
	ATF_CHECK_STREQ(p, "test");
	p = strtok(NULL, " ");
	ATF_CHECK_EQ(p, NULL);
}

ATF_TC(strtok_consecutive_delims);
ATF_TC_HEAD(strtok_consecutive_delims, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test strtok(3) with consecutive delimiters");
}

ATF_TC_BODY(strtok_consecutive_delims, tc)
{
	char str[] = ",,hello,,,world,,";
	char *p;

	p = strtok(str, ",");
	ATF_CHECK_STREQ(p, "hello");
	p = strtok(NULL, ",");
	ATF_CHECK_STREQ(p, "world");
	p = strtok(NULL, ",");
	ATF_CHECK_EQ(p, NULL);
}

ATF_TC(strtok_empty);
ATF_TC_HEAD(strtok_empty, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test strtok(3) with empty string");
}

ATF_TC_BODY(strtok_empty, tc)
{
	char str[] = "";
	char *p;

	p = strtok(str, ",");
	ATF_CHECK_EQ(p, NULL);
}

ATF_TC(strtok_no_delims);
ATF_TC_HEAD(strtok_no_delims, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test strtok(3) with string without delimiters");
}

ATF_TC_BODY(strtok_no_delims, tc)
{
	char str[] = "helloworld";
	char *p;

	p = strtok(str, ",");
	ATF_CHECK_STREQ(p, "helloworld");
	p = strtok(NULL, ",");
	ATF_CHECK_EQ(p, NULL);
}

ATF_TC(strtok_only_delims);
ATF_TC_HEAD(strtok_only_delims, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test strtok(3) with string having only delimiters");
}

ATF_TC_BODY(strtok_only_delims, tc)
{
	char str[] = ",,,,,,";
	char *p;

	p = strtok(str, ",");
	ATF_CHECK_EQ(p, NULL);
}

ATF_TC(strtok_r_basic);
ATF_TC_HEAD(strtok_r_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test strtok_r(3) with basic parsing");
}

ATF_TC_BODY(strtok_r_basic, tc)
{
	char str[] = "hello world test";
	char *p;
	char *lasts;

	p = strtok_r(str, " ", &lasts);
	ATF_CHECK_STREQ(p, "hello");
	p = strtok_r(NULL, " ", &lasts);
	ATF_CHECK_STREQ(p, "world");
	p = strtok_r(NULL, " ", &lasts);
	ATF_CHECK_STREQ(p, "test");
	p = strtok_r(NULL, " ", &lasts);
	ATF_CHECK_EQ(p, NULL);
}

ATF_TC(strtok_r_consecutive_delims);
ATF_TC_HEAD(strtok_r_consecutive_delims, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test strtok_r(3) with consecutive delimiters");
}

ATF_TC_BODY(strtok_r_consecutive_delims, tc)
{
	char str[] = ",,hello,,,world,,";
	char *p;
	char *lasts;

	p = strtok_r(str, ",", &lasts);
	ATF_CHECK_STREQ(p, "hello");
	p = strtok_r(NULL, ",", &lasts);
	ATF_CHECK_STREQ(p, "world");
	p = strtok_r(NULL, ",", &lasts);
	ATF_CHECK_EQ(p, NULL);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, strtok_basic);
	ATF_TP_ADD_TC(tp, strtok_consecutive_delims);
	ATF_TP_ADD_TC(tp, strtok_empty);
	ATF_TP_ADD_TC(tp, strtok_no_delims);
	ATF_TP_ADD_TC(tp, strtok_only_delims);

	ATF_TP_ADD_TC(tp, strtok_r_basic);
	ATF_TP_ADD_TC(tp, strtok_r_consecutive_delims);

	return atf_no_error();
}
