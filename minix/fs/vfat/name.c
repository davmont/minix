/* Name and time conversions for the vfat server.
 *
 * Long (VFAT) names are stored on disk as UTF-16 and exchanged with VFS as
 * UTF-8.  Short (8.3) names use the OEM code page; we map its upper half with
 * the classic CP437 table so accented 8.3 names round-trip to UTF-8.
 */
#include "fs.h"
#include <time.h>

/* CP437 (the canonical DOS OEM code page) upper half: byte 0x80..0xff ->
 * Unicode code point.  Used to render 8.3 names that contain OEM characters. */
static const uint16_t cp437_high[128] = {
	0x00c7,0x00fc,0x00e9,0x00e2,0x00e4,0x00e0,0x00e5,0x00e7,
	0x00ea,0x00eb,0x00e8,0x00ef,0x00ee,0x00ec,0x00c4,0x00c5,
	0x00c9,0x00e6,0x00c6,0x00f4,0x00f6,0x00f2,0x00fb,0x00f9,
	0x00ff,0x00d6,0x00dc,0x00a2,0x00a3,0x00a5,0x20a7,0x0192,
	0x00e1,0x00ed,0x00f3,0x00fa,0x00f1,0x00d1,0x00aa,0x00ba,
	0x00bf,0x2310,0x00ac,0x00bd,0x00bc,0x00a1,0x00ab,0x00bb,
	0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,
	0x2555,0x2563,0x2551,0x2557,0x255d,0x255c,0x255b,0x2510,
	0x2514,0x2534,0x252c,0x251c,0x2500,0x253c,0x255e,0x255f,
	0x255a,0x2554,0x2569,0x2566,0x2560,0x2550,0x256c,0x2567,
	0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256b,
	0x256a,0x2518,0x250c,0x2588,0x2584,0x258c,0x2590,0x2580,
	0x03b1,0x00df,0x0393,0x03c0,0x03a3,0x03c3,0x00b5,0x03c4,
	0x03a6,0x0398,0x03a9,0x03b4,0x221e,0x03c6,0x03b5,0x2229,
	0x2261,0x00b1,0x2265,0x2264,0x2320,0x2321,0x00f7,0x2248,
	0x00b0,0x2219,0x00b7,0x221a,0x207f,0x00b2,0x25a0,0x00a0
};

/*===========================================================================*
 *				put_utf8				     *
 *===========================================================================*/
static int put_utf8(uint32_t cp, unsigned char *out, size_t room)
{
/* Encode one Unicode code point as UTF-8 into out (room bytes).  Returns the
 * number of bytes written, or 0 if there is not enough room. */
	if (cp < 0x80) {
		if (room < 1) return 0;
		out[0] = (unsigned char) cp;
		return 1;
	} else if (cp < 0x800) {
		if (room < 2) return 0;
		out[0] = (unsigned char)(0xc0 | (cp >> 6));
		out[1] = (unsigned char)(0x80 | (cp & 0x3f));
		return 2;
	} else if (cp < 0x10000) {
		if (room < 3) return 0;
		out[0] = (unsigned char)(0xe0 | (cp >> 12));
		out[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
		out[2] = (unsigned char)(0x80 | (cp & 0x3f));
		return 3;
	} else {
		if (room < 4) return 0;
		out[0] = (unsigned char)(0xf0 | (cp >> 18));
		out[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3f));
		out[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
		out[3] = (unsigned char)(0x80 | (cp & 0x3f));
		return 4;
	}
}

/*===========================================================================*
 *				utf16_to_utf8				     *
 *===========================================================================*/
int utf16_to_utf8(const uint16_t *in, int inlen, char *out, size_t outsize)
{
/* Convert UTF-16 to a NUL-terminated UTF-8 string.  Returns the byte length,
 * or -1 if it would not fit. */
	size_t o = 0;
	int i, k;

	for (i = 0; i < inlen; i++) {
		uint32_t cp = in[i];

		if (cp >= 0xd800 && cp <= 0xdbff && i + 1 < inlen &&
		    in[i + 1] >= 0xdc00 && in[i + 1] <= 0xdfff)
			cp = 0x10000 + ((cp - 0xd800) << 10) + (in[++i] - 0xdc00);

		if ((k = put_utf8(cp, (unsigned char *) out + o,
		    outsize - 1 - o)) == 0)
			return -1;
		o += k;
	}
	out[o] = '\0';
	return (int) o;
}

/*===========================================================================*
 *				utf8_to_utf16				     *
 *===========================================================================*/
int utf8_to_utf16(const char *in, uint16_t *out, int outmax)
{
/* Convert a NUL-terminated UTF-8 string to UTF-16.  Returns the number of code
 * units, or -1 on overflow or malformed input. */
	const unsigned char *p = (const unsigned char *) in;
	int n = 0;

	while (*p != '\0') {
		uint32_t cp;
		int extra;

		if (*p < 0x80) { cp = *p++; extra = 0; }
		else if ((*p & 0xe0) == 0xc0) { cp = *p++ & 0x1f; extra = 1; }
		else if ((*p & 0xf0) == 0xe0) { cp = *p++ & 0x0f; extra = 2; }
		else if ((*p & 0xf8) == 0xf0) { cp = *p++ & 0x07; extra = 3; }
		else return -1;

		while (extra-- > 0) {
			if ((*p & 0xc0) != 0x80) return -1;
			cp = (cp << 6) | (*p++ & 0x3f);
		}

		if (cp < 0x10000) {
			if (n >= outmax) return -1;
			out[n++] = (uint16_t) cp;
		} else {
			if (n + 2 > outmax) return -1;
			cp -= 0x10000;
			out[n++] = (uint16_t)(0xd800 + (cp >> 10));
			out[n++] = (uint16_t)(0xdc00 + (cp & 0x3ff));
		}
	}
	return n;
}

/*===========================================================================*
 *				days_from_civil				     *
 *===========================================================================*/
static long days_from_civil(int y, unsigned m, unsigned d)
{
/* Days since 1970-01-01 (Howard Hinnant's algorithm), UTC. */
	long yy = y - (m <= 2);
	long era = (yy >= 0 ? yy : yy - 399) / 400;
	unsigned yoe = (unsigned)(yy - era * 400);
	unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

	return era * 146097L + (long) doe - 719468L;
}

/*===========================================================================*
 *				dos2unixtime				     *
 *===========================================================================*/
void dos2unixtime(unsigned int dd, unsigned int dt, struct timespec *tsp)
{
/* Convert a DOS date (dd) and time (dt) into a Unix timestamp (UTC). */
	int year, mon, day, hour, min, sec;
	long days;

	if (dd == 0) {
		tsp->tv_sec = 0;
		tsp->tv_nsec = 0;
		return;
	}

	year = ((dd & DD_YEAR_MASK) >> DD_YEAR_SHIFT) + 1980;
	mon = (dd & DD_MONTH_MASK) >> DD_MONTH_SHIFT;
	day = (dd & DD_DAY_MASK) >> DD_DAY_SHIFT;
	if (mon < 1) mon = 1;
	if (day < 1) day = 1;

	hour = (dt & DT_HOURS_MASK) >> DT_HOURS_SHIFT;
	min = (dt & DT_MINUTES_MASK) >> DT_MINUTES_SHIFT;
	sec = ((dt & DT_2SECONDS_MASK) >> DT_2SECONDS_SHIFT) * 2;

	days = days_from_civil(year, (unsigned) mon, (unsigned) day);
	tsp->tv_sec = days * 86400L + hour * 3600L + min * 60L + sec;
	tsp->tv_nsec = 0;
}

/*===========================================================================*
 *				unix2dostime				     *
 *===========================================================================*/
void unix2dostime(time_t t, uint16_t *ddp, uint16_t *dtp)
{
/* Convert a Unix timestamp (UTC) into DOS date (dd) and time (dt) words.
 * DOS cannot represent dates before 1980; such times become 0.
 */
	struct tm tmv;

	if (t < 0) {
		*ddp = 0;
		*dtp = 0;
		return;
	}

	(void) gmtime_r(&t, &tmv);

	if (tmv.tm_year + 1900 < 1980 || tmv.tm_year + 1900 > 1980 + 127) {
		*ddp = 0;
		*dtp = 0;
		return;
	}

	*ddp = (tmv.tm_mday << DD_DAY_SHIFT) +
	    ((tmv.tm_mon + 1) << DD_MONTH_SHIFT) +
	    ((tmv.tm_year + 1900 - 1980) << DD_YEAR_SHIFT);
	*dtp = ((tmv.tm_sec / 2) << DT_2SECONDS_SHIFT) +
	    (tmv.tm_min << DT_MINUTES_SHIFT) +
	    (tmv.tm_hour << DT_HOURS_SHIFT);
}

/*===========================================================================*
 *				dos2unixfn				     *
 *===========================================================================*/
int dos2unixfn(const unsigned char dn[11], unsigned char *un, int lower)
{
/* Convert a DOS 8.3 directory-entry name (8 name + 3 ext, blank padded) into a
 * NUL-terminated UTF-8 string.  ASCII letters are lower-cased; OEM bytes
 * (0x80..0xff) are mapped through CP437 to Unicode.  Returns the byte length.
 */
	int i, j;
	unsigned char c;
	unsigned char *start = un;

	(void) lower;

	/* Name part. */
	for (i = 0, j = 7; i <= j; j--)
		if (dn[j] != ' ')
			break;
	for (i = 0; i <= j; i++) {
		c = dn[i];
		if (i == 0 && c == SLOT_E5)	/* 0x05 stands for a leading 0xe5 */
			c = 0xe5;
		if (c >= 'A' && c <= 'Z')
			*un++ = c + ('a' - 'A');
		else if (c >= 0x80)
			un += put_utf8(cp437_high[c - 0x80], un, 4);
		else
			*un++ = c;
	}

	/* Extension part. */
	for (i = 8, j = 10; i <= j; j--)
		if (dn[j] != ' ')
			break;
	if (j >= 8) {
		*un++ = '.';
		for (i = 8; i <= j; i++) {
			c = dn[i];
			if (c >= 'A' && c <= 'Z')
				*un++ = c + ('a' - 'A');
			else if (c >= 0x80)
				un += put_utf8(cp437_high[c - 0x80], un, 4);
			else
				*un++ = c;
		}
	}

	*un = '\0';
	return (int)(un - start);
}

/*===========================================================================*
 *				winchksum				     *
 *===========================================================================*/
uint8_t winchksum(const unsigned char *name)
{
/* Compute the checksum of an 8.3 name as stored in winentry weChksum. */
	int i;
	uint8_t s = 0;

	for (i = 0; i < 11; i++)
		s = ((s & 1) ? 0x80 : 0) + (s >> 1) + name[i];

	return s;
}

/*===========================================================================*
 *				win2unixfn				     *
 *===========================================================================*/
int win2wchar(const struct winentry *wep, uint16_t *dst, int dstmax, int chksum)
{
/* Extract the (up to) 13 UTF-16 code units carried by one long-name slot into
 * 'dst' (dstmax room).  Returns the number of code units placed (stopping at a
 * NUL/0xffff terminator), or -1 on a checksum mismatch.  The caller assembles
 * the slots into a UTF-16 name and converts the whole to UTF-8 once. */
	const uint8_t *parts[3];
	int partlen[3] = { 5, 6, 2 };
	int p, k, n = 0;

	if (wep->weChksum != chksum)
		return -1;

	parts[0] = wep->wePart1;
	parts[1] = wep->wePart2;
	parts[2] = wep->wePart3;

	for (p = 0; p < 3; p++) {
		for (k = 0; k < partlen[p]; k++) {
			uint16_t wc = parts[p][k * 2] |
			    ((uint16_t) parts[p][k * 2 + 1] << 8);

			if (wc == 0 || wc == 0xffff)
				return n;
			if (n >= dstmax)
				return n;
			dst[n++] = wc;
		}
	}

	return n;
}
