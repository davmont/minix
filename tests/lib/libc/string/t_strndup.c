/* $NetBSD$ */

#include <atf-c.h>
#include <string.h>
#include <stdlib.h>

ATF_TC(strndup_basic);
ATF_TC_HEAD(strndup_basic, tc)
{
        atf_tc_set_md_var(tc, "descr", "Test strndup(3) results");
}

ATF_TC_BODY(strndup_basic, tc)
{
	const char *orig = "abcdefghijklmnopqrstuvwxyz";
	char *dup;

	/* Test copying a string that is shorter than n */
	dup = strndup(orig, 100);
	ATF_REQUIRE(dup != NULL);
	ATF_REQUIRE(strcmp(orig, dup) == 0);
	ATF_REQUIRE(dup != orig);
	free(dup);

	/* Test copying exactly the length of the string */
	dup = strndup(orig, strlen(orig));
	ATF_REQUIRE(dup != NULL);
	ATF_REQUIRE(strcmp(orig, dup) == 0);
	ATF_REQUIRE(dup != orig);
	free(dup);

	/* Test copying a string that is longer than n */
	dup = strndup(orig, 10);
	ATF_REQUIRE(dup != NULL);
	ATF_REQUIRE(strncmp(orig, dup, 10) == 0);
	ATF_REQUIRE(dup[10] == '\0');
	ATF_REQUIRE(strlen(dup) == 10);
	free(dup);

	/* Test copying 0 characters */
	dup = strndup(orig, 0);
	ATF_REQUIRE(dup != NULL);
	ATF_REQUIRE(dup[0] == '\0');
	ATF_REQUIRE(strlen(dup) == 0);
	free(dup);

	/* Test empty string */
	dup = strndup("", 10);
	ATF_REQUIRE(dup != NULL);
	ATF_REQUIRE(dup[0] == '\0');
	ATF_REQUIRE(strlen(dup) == 0);
	free(dup);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, strndup_basic);
	return atf_no_error();
}
