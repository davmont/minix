/* $NetBSD$ */

#include <atf-c.h>
#include <wchar.h>

ATF_TC(wcschr_basic);
ATF_TC_HEAD(wcschr_basic, tc)
{
        atf_tc_set_md_var(tc, "descr", "Test wcschr(3) results");
}

ATF_TC_BODY(wcschr_basic, tc)
{
        wchar_t str[] = L"abcdefg";

        ATF_CHECK(wcschr(str, L'a') == &str[0]);
        ATF_CHECK(wcschr(str, L'd') == &str[3]);
        ATF_CHECK(wcschr(str, L'g') == &str[6]);
        ATF_CHECK(wcschr(str, L'\0') == &str[7]);
        ATF_CHECK(wcschr(str, L'x') == NULL);

        wchar_t empty[] = L"";
        ATF_CHECK(wcschr(empty, L'a') == NULL);
        ATF_CHECK(wcschr(empty, L'\0') == &empty[0]);
}

ATF_TP_ADD_TCS(tp)
{
        ATF_TP_ADD_TC(tp, wcschr_basic);

        return atf_no_error();
}
