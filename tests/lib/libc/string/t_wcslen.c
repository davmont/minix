/* $NetBSD: t_wcslen.c $ */

#include <atf-c.h>
#include <wchar.h>

ATF_TC(wcslen_basic);
ATF_TC_HEAD(wcslen_basic, tc)
{
        atf_tc_set_md_var(tc, "descr", "Test wcslen(3) results");
}

ATF_TC_BODY(wcslen_basic, tc)
{
	ATF_CHECK(wcslen(L"") == 0);
	ATF_CHECK(wcslen(L"a") == 1);
	ATF_CHECK(wcslen(L"ab") == 2);
	ATF_CHECK(wcslen(L"abc") == 3);
	ATF_CHECK(wcslen(L"abcd") == 4);
	ATF_CHECK(wcslen(L"abcde") == 5);
	ATF_CHECK(wcslen(L"abcdef") == 6);
	ATF_CHECK(wcslen(L"abcdefg") == 7);

	wchar_t buf[128];
	for (size_t i = 0; i < 127; i++) {
		buf[i] = L'x';
	}
	buf[127] = L'\0';
	ATF_CHECK(wcslen(buf) == 127);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, wcslen_basic);

	return atf_no_error();
}
