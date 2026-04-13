/*	$NetBSD$	*/

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

#include <atf-c.h>
#include <ctype.h>
#include <string.h>

#include "cd9660.h"

ATF_TC(cd9660_valid_a_chars_basic);
ATF_TC_HEAD(cd9660_valid_a_chars_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test cd9660_valid_a_chars basic functionality");
}

ATF_TC_BODY(cd9660_valid_a_chars_basic, tc)
{
	/* Valid a-chars return 1 */
	ATF_CHECK_EQ(cd9660_valid_a_chars("ABCDEF12345_"), 1);
	ATF_CHECK_EQ(cd9660_valid_a_chars(" !\"%&'()*+,-./0123456789:;<=>?"), 1);
	ATF_CHECK_EQ(cd9660_valid_a_chars(""), 1);

	/* Valid a-chars with lowercase return 2 */
	ATF_CHECK_EQ(cd9660_valid_a_chars("abc"), 2);
	ATF_CHECK_EQ(cd9660_valid_a_chars("AbC"), 2);
	ATF_CHECK_EQ(cd9660_valid_a_chars("a_1"), 2);

	/* Invalid characters return 0 */
	ATF_CHECK_EQ(cd9660_valid_a_chars("abc@"), 0);
	ATF_CHECK_EQ(cd9660_valid_a_chars("ABC["), 0);
	ATF_CHECK_EQ(cd9660_valid_a_chars("ABC#"), 0);
	ATF_CHECK_EQ(cd9660_valid_a_chars("ABC$"), 0);
	ATF_CHECK_EQ(cd9660_valid_a_chars("ABC\\"), 0);
	ATF_CHECK_EQ(cd9660_valid_a_chars("ABC]"), 0);
	ATF_CHECK_EQ(cd9660_valid_a_chars("ABC^"), 0);
	ATF_CHECK_EQ(cd9660_valid_a_chars("ABC`"), 0);
	ATF_CHECK_EQ(cd9660_valid_a_chars("ABC{"), 0);
	ATF_CHECK_EQ(cd9660_valid_a_chars("ABC|"), 0);
	ATF_CHECK_EQ(cd9660_valid_a_chars("ABC}"), 0);
	ATF_CHECK_EQ(cd9660_valid_a_chars("ABC~"), 0);

	/* Boundary tests for valid ranges */
	ATF_CHECK_EQ(cd9660_valid_a_chars(" "), 1);
	ATF_CHECK_EQ(cd9660_valid_a_chars("\""), 1);
	ATF_CHECK_EQ(cd9660_valid_a_chars("%"), 1);
	ATF_CHECK_EQ(cd9660_valid_a_chars("?"), 1);
	ATF_CHECK_EQ(cd9660_valid_a_chars("A"), 1);
	ATF_CHECK_EQ(cd9660_valid_a_chars("Z"), 1);
	ATF_CHECK_EQ(cd9660_valid_a_chars("a"), 2);
	ATF_CHECK_EQ(cd9660_valid_a_chars("z"), 2);

	/* Boundary tests for explicitly excluded characters */
	ATF_CHECK_EQ(cd9660_valid_a_chars("#"), 0);
	ATF_CHECK_EQ(cd9660_valid_a_chars("$"), 0);

	/* Internal padding null bytes testing via string literals where applicable
	 * Since the string is passed as const char*, intermediate nulls cut the string off,
	 * so "A\0#" will return 1, because it only tests 'A'.
	 */
	ATF_CHECK_EQ(cd9660_valid_a_chars("A\0#"), 1);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, cd9660_valid_a_chars_basic);

	return atf_no_error();
}
