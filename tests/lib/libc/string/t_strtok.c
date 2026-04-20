/*	$NetBSD: t_strtok.c,v 1.1 2024/05/11 00:00:00 jules Exp $	*/

/*-
 * Copyright (c) 2024 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jules.
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
	char buf[] = "abc:def:ghi";
	char *p;

	p = strtok(buf, ":");
	ATF_CHECK(p != NULL);
	ATF_CHECK(strcmp(p, "abc") == 0);
	p = strtok(NULL, ":");
	ATF_CHECK(p != NULL);
	ATF_CHECK(strcmp(p, "def") == 0);
	p = strtok(NULL, ":");
	ATF_CHECK(p != NULL);
	ATF_CHECK(strcmp(p, "ghi") == 0);
	p = strtok(NULL, ":");
	ATF_CHECK(p == NULL);
}

ATF_TC(strtok_r_basic);
ATF_TC_HEAD(strtok_r_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test strtok_r(3) basic functionality");
}

ATF_TC_BODY(strtok_r_basic, tc)
{
	char buf[] = "abc:def:ghi";
	char *last, *p;

	p = strtok_r(buf, ":", &last);
	ATF_CHECK(p != NULL);
	ATF_CHECK(strcmp(p, "abc") == 0);
	p = strtok_r(NULL, ":", &last);
	ATF_CHECK(p != NULL);
	ATF_CHECK(strcmp(p, "def") == 0);
	p = strtok_r(NULL, ":", &last);
	ATF_CHECK(p != NULL);
	ATF_CHECK(strcmp(p, "ghi") == 0);
	p = strtok_r(NULL, ":", &last);
	ATF_CHECK(p == NULL);
}

ATF_TC(strtok_r_complex);
ATF_TC_HEAD(strtok_r_complex, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test strtok_r(3) with complex delimiters");
}

ATF_TC_BODY(strtok_r_complex, tc)
{
	char buf[] = "  abc  ,,def,,  ghi  ";
	char *last, *p;

	p = strtok_r(buf, " ,", &last);
	ATF_CHECK(p != NULL);
	ATF_CHECK(strcmp(p, "abc") == 0);
	p = strtok_r(NULL, " ,", &last);
	ATF_CHECK(p != NULL);
	ATF_CHECK(strcmp(p, "def") == 0);
	p = strtok_r(NULL, " ,", &last);
	ATF_CHECK(p != NULL);
	ATF_CHECK(strcmp(p, "ghi") == 0);
	p = strtok_r(NULL, " ,", &last);
	ATF_CHECK(p == NULL);
}

ATF_TC(strtok_r_no_tokens);
ATF_TC_HEAD(strtok_r_no_tokens, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test strtok_r(3) with no tokens");
}

ATF_TC_BODY(strtok_r_no_tokens, tc)
{
	char buf[] = ",,,";
	char *last, *p;

	p = strtok_r(buf, ",", &last);
	ATF_CHECK(p == NULL);
}

ATF_TC(strtok_r_empty);
ATF_TC_HEAD(strtok_r_empty, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test strtok_r(3) with empty string");
}

ATF_TC_BODY(strtok_r_empty, tc)
{
	char buf[] = "";
	char *last, *p;

	p = strtok_r(buf, ",", &last);
	ATF_CHECK(p == NULL);
}

ATF_TC(strtok_r_reentrant);
ATF_TC_HEAD(strtok_r_reentrant, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test strtok_r(3) reentrancy");
}

ATF_TC_BODY(strtok_r_reentrant, tc)
{
	char buf1[] = "a:b:c";
	char buf2[] = "1,2,3";
	char *last1, *last2;
	char *p1, *p2;

	p1 = strtok_r(buf1, ":", &last1);
	ATF_CHECK(p1 != NULL);
	ATF_CHECK(strcmp(p1, "a") == 0);

	p2 = strtok_r(buf2, ",", &last2);
	ATF_CHECK(p2 != NULL);
	ATF_CHECK(strcmp(p2, "1") == 0);

	p1 = strtok_r(NULL, ":", &last1);
	ATF_CHECK(p1 != NULL);
	ATF_CHECK(strcmp(p1, "b") == 0);

	p2 = strtok_r(NULL, ",", &last2);
	ATF_CHECK(p2 != NULL);
	ATF_CHECK(strcmp(p2, "2") == 0);

	p1 = strtok_r(NULL, ":", &last1);
	ATF_CHECK(p1 != NULL);
	ATF_CHECK(strcmp(p1, "c") == 0);

	p2 = strtok_r(NULL, ",", &last2);
	ATF_CHECK(p2 != NULL);
	ATF_CHECK(strcmp(p2, "3") == 0);

	p1 = strtok_r(NULL, ":", &last1);
	ATF_CHECK(p1 == NULL);

	p2 = strtok_r(NULL, ",", &last2);
	ATF_CHECK(p2 == NULL);
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
	ATF_TP_ADD_TC(tp, strtok_r_basic);
	ATF_TP_ADD_TC(tp, strtok_r_complex);
	ATF_TP_ADD_TC(tp, strtok_r_no_tokens);
	ATF_TP_ADD_TC(tp, strtok_r_empty);
	ATF_TP_ADD_TC(tp, strtok_r_reentrant);

	return atf_no_error();
}
