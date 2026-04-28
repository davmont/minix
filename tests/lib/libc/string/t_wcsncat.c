/*	$NetBSD$	*/

#include <atf-c.h>
#include <wchar.h>

ATF_TC(wcsncat_basic);
ATF_TC_HEAD(wcsncat_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test wcsncat(3) results");
}

ATF_TC_BODY(wcsncat_basic, tc)
{
	wchar_t buf[100];

	/* Basic concatenation */
	(void)wcscpy(buf, L"abcd");
	ATF_CHECK(wcsncat(buf, L"efgh", 10) == buf);
	ATF_CHECK(wcscmp(buf, L"abcdefgh") == 0);

	/* n smaller than source length */
	(void)wcscpy(buf, L"abcd");
	ATF_CHECK(wcsncat(buf, L"efgh", 2) == buf);
	ATF_CHECK(wcscmp(buf, L"abcdef") == 0);

	/* n equal to source length */
	(void)wcscpy(buf, L"abcd");
	ATF_CHECK(wcsncat(buf, L"efgh", 4) == buf);
	ATF_CHECK(wcscmp(buf, L"abcdefgh") == 0);

	/* n is 0 */
	(void)wcscpy(buf, L"abcd");
	ATF_CHECK(wcsncat(buf, L"efgh", 0) == buf);
	ATF_CHECK(wcscmp(buf, L"abcd") == 0);

	/* Empty source string */
	(void)wcscpy(buf, L"abcd");
	ATF_CHECK(wcsncat(buf, L"", 10) == buf);
	ATF_CHECK(wcscmp(buf, L"abcd") == 0);

	/* Empty destination string */
	(void)wcscpy(buf, L"");
	ATF_CHECK(wcsncat(buf, L"abcd", 10) == buf);
	ATF_CHECK(wcscmp(buf, L"abcd") == 0);

	/* Both empty */
	(void)wcscpy(buf, L"");
	ATF_CHECK(wcsncat(buf, L"", 10) == buf);
	ATF_CHECK(wcscmp(buf, L"") == 0);

	/* n is 1 */
	(void)wcscpy(buf, L"abcd");
	ATF_CHECK(wcsncat(buf, L"e", 1) == buf);
	ATF_CHECK(wcscmp(buf, L"abcde") == 0);

	/* Large n */
	(void)wcscpy(buf, L"abcd");
	ATF_CHECK(wcsncat(buf, L"efgh", 100) == buf);
	ATF_CHECK(wcscmp(buf, L"abcdefgh") == 0);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, wcsncat_basic);
	return atf_no_error();
}
