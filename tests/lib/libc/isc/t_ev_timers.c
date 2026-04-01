/* \$NetBSD\$ */

/*
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
 * \`\`AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
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
__RCSID("\$NetBSD\$");

#include <atf-c.h>
#include <sys/time.h>
#include <time.h>

#include <isc/eventlib.h>

ATF_TC(evTimeSpec_basic);
ATF_TC_HEAD(evTimeSpec_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test evTimeSpec(3) basic conversion");
}

ATF_TC_BODY(evTimeSpec_basic, tc)
{
	struct timeval tv;
	struct timespec ts;

	tv.tv_sec = 1234567890;
	tv.tv_usec = 123456;
	ts = evTimeSpec(tv);

	ATF_CHECK_EQ(ts.tv_sec, tv.tv_sec);
	ATF_CHECK_EQ(ts.tv_nsec, (long)tv.tv_usec * 1000);
}

ATF_TC(evTimeSpec_zero);
ATF_TC_HEAD(evTimeSpec_zero, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test evTimeSpec(3) with zero values");
}

ATF_TC_BODY(evTimeSpec_zero, tc)
{
	struct timeval tv;
	struct timespec ts;

	tv.tv_sec = 0;
	tv.tv_usec = 0;
	ts = evTimeSpec(tv);

	ATF_CHECK_EQ(ts.tv_sec, 0);
	ATF_CHECK_EQ(ts.tv_nsec, 0);
}

ATF_TC(evTimeSpec_max_usec);
ATF_TC_HEAD(evTimeSpec_max_usec, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test evTimeSpec(3) with 999999 usec");
}

ATF_TC_BODY(evTimeSpec_max_usec, tc)
{
	struct timeval tv;
	struct timespec ts;

	tv.tv_sec = 100;
	tv.tv_usec = 999999;
	ts = evTimeSpec(tv);

	ATF_CHECK_EQ(ts.tv_sec, 100);
	ATF_CHECK_EQ(ts.tv_nsec, 999999000);
}

ATF_TC(evTimeVal_basic);
ATF_TC_HEAD(evTimeVal_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test evTimeVal(3) basic conversion");
}

ATF_TC_BODY(evTimeVal_basic, tc)
{
	struct timespec ts;
	struct timeval tv;

	ts.tv_sec = 1234567890;
	ts.tv_nsec = 123456000;
	tv = evTimeVal(ts);

	ATF_CHECK_EQ(tv.tv_sec, ts.tv_sec);
	ATF_CHECK_EQ(tv.tv_usec, (long)(ts.tv_nsec / 1000));
}

ATF_TC(evTimeVal_zero);
ATF_TC_HEAD(evTimeVal_zero, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test evTimeVal(3) with zero values");
}

ATF_TC_BODY(evTimeVal_zero, tc)
{
	struct timespec ts;
	struct timeval tv;

	ts.tv_sec = 0;
	ts.tv_nsec = 0;
	tv = evTimeVal(ts);

	ATF_CHECK_EQ(tv.tv_sec, 0);
	ATF_CHECK_EQ(tv.tv_usec, 0);
}

ATF_TC(evConsTime_basic);
ATF_TC_HEAD(evConsTime_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test evConsTime(3)");
}

ATF_TC_BODY(evConsTime_basic, tc)
{
	struct timespec ts;

	ts = evConsTime(123, 456);
	ATF_CHECK_EQ(ts.tv_sec, 123);
	ATF_CHECK_EQ(ts.tv_nsec, 456);
}

ATF_TC(evAddTime_basic);
ATF_TC_HEAD(evAddTime_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test evAddTime(3)");
}

ATF_TC_BODY(evAddTime_basic, tc)
{
	struct timespec t1, t2, ts;

	t1.tv_sec = 1;
	t1.tv_nsec = 500000000;
	t2.tv_sec = 2;
	t2.tv_nsec = 600000000;
	ts = evAddTime(t1, t2);

	ATF_CHECK_EQ(ts.tv_sec, 4);
	ATF_CHECK_EQ(ts.tv_nsec, 100000000);
}

ATF_TC(evAddTime_zero);
ATF_TC_HEAD(evAddTime_zero, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test evAddTime(3) with zero values");
}

ATF_TC_BODY(evAddTime_zero, tc)
{
	struct timespec t1, t2, ts;

	t1.tv_sec = 0;
	t1.tv_nsec = 0;
	t2.tv_sec = 0;
	t2.tv_nsec = 0;
	ts = evAddTime(t1, t2);

	ATF_CHECK_EQ(ts.tv_sec, 0);
	ATF_CHECK_EQ(ts.tv_nsec, 0);
}

ATF_TC(evAddTime_nsec_overflow);
ATF_TC_HEAD(evAddTime_nsec_overflow, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test evAddTime(3) with nsec overflow");
}

ATF_TC_BODY(evAddTime_nsec_overflow, tc)
{
	struct timespec t1, t2, ts;

	t1.tv_sec = 1;
	t1.tv_nsec = 500000000;
	t2.tv_sec = 2;
	t2.tv_nsec = 500000000;
	ts = evAddTime(t1, t2);

	ATF_CHECK_EQ(ts.tv_sec, 4);
	ATF_CHECK_EQ(ts.tv_nsec, 0);
}

ATF_TC(evSubTime_basic);
ATF_TC_HEAD(evSubTime_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test evSubTime(3)");
}

ATF_TC_BODY(evSubTime_basic, tc)
{
	struct timespec t1, t2, ts;

	t1.tv_sec = 4;
	t1.tv_nsec = 100000000;
	t2.tv_sec = 2;
	t2.tv_nsec = 600000000;
	ts = evSubTime(t1, t2);

	ATF_CHECK_EQ(ts.tv_sec, 1);
	ATF_CHECK_EQ(ts.tv_nsec, 500000000);
}

ATF_TC(evCmpTime_basic);
ATF_TC_HEAD(evCmpTime_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test evCmpTime(3)");
}

ATF_TC_BODY(evCmpTime_basic, tc)
{
	struct timespec t1, t2;

	t1.tv_sec = 1; t1.tv_nsec = 100;
	t2.tv_sec = 1; t2.tv_nsec = 200;
	ATF_CHECK(evCmpTime(t1, t2) < 0);
	ATF_CHECK(evCmpTime(t2, t1) > 0);
	ATF_CHECK(evCmpTime(t1, t1) == 0);

	t1.tv_sec = 2; t1.tv_nsec = 100;
	t2.tv_sec = 1; t2.tv_nsec = 100;
	ATF_CHECK(evCmpTime(t1, t2) > 0);
	ATF_CHECK(evCmpTime(t2, t1) < 0);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, evTimeSpec_basic);
	ATF_TP_ADD_TC(tp, evTimeSpec_zero);
	ATF_TP_ADD_TC(tp, evTimeSpec_max_usec);
	ATF_TP_ADD_TC(tp, evTimeVal_basic);
	ATF_TP_ADD_TC(tp, evTimeVal_zero);
	ATF_TP_ADD_TC(tp, evConsTime_basic);
	ATF_TP_ADD_TC(tp, evAddTime_basic);
	ATF_TP_ADD_TC(tp, evAddTime_zero);
	ATF_TP_ADD_TC(tp, evAddTime_nsec_overflow);
	ATF_TP_ADD_TC(tp, evSubTime_basic);
	ATF_TP_ADD_TC(tp, evCmpTime_basic);

	return atf_no_error();
}
