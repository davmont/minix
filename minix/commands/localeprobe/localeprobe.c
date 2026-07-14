/*
 * localeprobe -- can a statically linked program use a UTF-8 locale and iconv?
 *
 * Until the citrus modules were compiled into libc, it could not.  citrus loads
 * the LC_CTYPE encoding handlers and the iconv converters as shared modules
 * dlopen'd from /usr/lib/i18n, and MINIX links its programs statically -- so
 * every module load failed.  The symptoms were quiet rather than loud:
 *
 *   - setlocale(LC_CTYPE, "en_US.UTF-8") *appeared to succeed*: it returns the
 *     locale name, but LC_CTYPE silently stayed in C, so nl_langinfo(CODESET)
 *     kept answering "646".  Checking setlocale's return value tells you nothing;
 *     the codeset is what gives it away.
 *   - iconv_open() failed with EINVAL for every pair, even though the conversion
 *     tables in /usr/share/i18n were installed and correct.
 *
 * So this checks the codeset, not just the return value, and then actually
 * converts and decodes some non-ASCII text.
 */
#include <errno.h>
#include <iconv.h>
#include <langinfo.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int failures;

static void
check(const char *what, int ok, const char *detail)
{
	printf("  %-44s -> %s", what, ok ? "OK" : "FAIL");
	if (detail != NULL && *detail != '\0')
		printf("  (%s)", detail);
	putchar('\n');
	if (!ok)
		failures++;
}

int
main(void)
{
	/* "héllo wörld" in UTF-8, spelled out so the file's own encoding cannot
	 * quietly change what is being tested. */
	static const char utf8[] = "h\xc3\xa9llo w\xc3\xb6rld";
	char inbuf[64], outbuf[64];
	const char *codeset, *loc;
	const char *in;
	char *out;
	size_t inleft, outleft, n;
	wchar_t wcs[64];
	iconv_t cd;

	printf("\nlocaleprobe: UTF-8 locale and iconv in a static binary\n\n");

	errno = 0;
	loc = setlocale(LC_ALL, "en_US.UTF-8");

	check("setlocale(LC_ALL, \"en_US.UTF-8\") returned", loc != NULL,
	    loc ? loc : "NULL");

	/*
	 * The real test.  setlocale() above would have "succeeded" even when the
	 * ctype module could not be loaded; the codeset is what tells the truth.
	 */
	codeset = nl_langinfo(CODESET);
	check("nl_langinfo(CODESET) is UTF-8",
	    codeset != NULL && strcmp(codeset, "UTF-8") == 0,
	    codeset ? codeset : "NULL");

	/* Multibyte decoding: needs the UTF8 ctype module to be live. */
	n = mbstowcs(wcs, utf8, sizeof(wcs) / sizeof(wcs[0]));
	check("mbstowcs decodes 11 chars from 13 bytes", n == 11,
	    n == (size_t)-1 ? "invalid multibyte sequence" : NULL);
	check("mbstowcs got U+00E9 (e-acute)", n != (size_t)-1 && wcs[1] == 0x00E9,
	    NULL);
	/*
	 * 6, not 4: citrus's UTF8 module still advertises the historical 6-byte
	 * form of UTF-8 (_ENCODING_MB_CUR_MAX), even though Unicode has been
	 * capped at 4 bytes since 2003.  What matters here is that it is no
	 * longer 1, which is what the C locale reports.
	 */
	check("MB_CUR_MAX is 6 (UTF-8, not the C locale's 1)",
	    MB_CUR_MAX == 6, NULL);

	/* iconv: this used to fail at iconv_open() for every pair. */
	cd = iconv_open("ISO-8859-1", "UTF-8");
	check("iconv_open(ISO-8859-1 <- UTF-8)", cd != (iconv_t)-1,
	    cd == (iconv_t)-1 ? strerror(errno) : NULL);

	if (cd != (iconv_t)-1) {
		strlcpy(inbuf, utf8, sizeof(inbuf));
		memset(outbuf, 0, sizeof(outbuf));
		in = inbuf;
		out = outbuf;
		inleft = strlen(inbuf);
		outleft = sizeof(outbuf) - 1;

		n = iconv(cd, &in, &inleft, &out, &outleft);
		check("iconv() converted the string", n != (size_t)-1,
		    n == (size_t)-1 ? strerror(errno) : NULL);

		/* U+00E9 must come out as the single byte 0xE9 in ISO-8859-1. */
		check("e-acute became one byte 0xE9",
		    (unsigned char)outbuf[1] == 0xE9 && strlen(outbuf) == 11,
		    NULL);
		iconv_close(cd);
	}

	/* The conversion that GLib kept failing on: the C locale's codeset. */
	cd = iconv_open("UTF-8", "646");
	check("iconv_open(UTF-8 <- 646), the one GLib needs",
	    cd != (iconv_t)-1, cd == (iconv_t)-1 ? strerror(errno) : NULL);
	if (cd != (iconv_t)-1)
		iconv_close(cd);

	printf("\nlocaleprobe: %s\n\n",
	    failures == 0 ? "ALL PASS" : "FAILED");

	return failures == 0 ? 0 : 1;
}
