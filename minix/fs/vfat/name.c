/* Name and time conversions for the vfat server.
 *
 * Derived from NetBSD's msdosfs_conv.c.  For phase 1 we fold names to 7-bit
 * ASCII (non-ASCII bytes become '?'); full UTF-8/codepage handling is a later
 * concern.
 */
#include "fs.h"
#include <time.h>

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
/* Convert a DOS 8.3 directory-entry name (8 name + 3 ext, blank padded) into
 * a NUL-terminated string.  Returns the length.  'lower' is currently advisory.
 */
	int i, j;
	unsigned char c;
	unsigned char *start = un;

	(void) lower;

	/* The first byte 0x05 actually means 0xe5 (a valid leading char). */
	/* Name part. */
	for (i = 0, j = 7; i <= j; j--)
		if (dn[j] != ' ')
			break;
	for (i = 0; i <= j; i++) {
		c = dn[i];
		if (i == 0 && c == SLOT_E5)
			c = 0xe5;
		if (c >= 'A' && c <= 'Z')
			c += 'a' - 'A';
		else if (c >= 0x80)
			c = '?';
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
				c += 'a' - 'A';
			else if (c >= 0x80)
				c = '?';
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
int win2unixfn(const struct winentry *wep, unsigned char *un, size_t unsize,
	int chksum)
{
/* Extract the (up to) 13 characters carried by one long-name slot into the
 * caller's buffer at the slot's position.  un points at the start of this
 * slot's chars; unsize is the remaining room.  Returns the number of valid
 * characters placed (stopping at a NUL/0xffff terminator), or -1 on a
 * checksum mismatch.
 */
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
			if ((size_t) n >= unsize)
				return n;
			un[n++] = (wc < 0x80) ? (unsigned char) wc : '?';
		}
	}

	return n;
}
