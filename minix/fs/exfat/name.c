/* Name handling for the exFAT server: the up-case table, UTF-16 <-> UTF-8
 * conversion, the exFAT name hash, and timestamp conversion.
 */
#include "fs.h"
#include <sys/time.h>

/* cluster.c helper: read bytes from a cluster chain. */
int chain_read(uint32_t start, int contig, uint64_t off, void *buf, size_t len);

/*===========================================================================*
 *				upcase_init				     *
 *===========================================================================*/
int upcase_init(void)
{
/* Allocate and fill the up-case table.  The caller (scan_root_meta) has stored
 * the table's location in pmp->pm_upcase_cluster/length; here we read it and
 * decompress it into a flat 65536-entry code-unit map.  If no table is present
 * or it cannot be read, fall back to a plain identity map with ASCII folding,
 * so the server still works for ASCII names. */
	uint16_t *tab;
	uint8_t *raw = NULL;
	unsigned i;
	uint32_t index;
	uint64_t len;

	if ((tab = malloc(65536 * sizeof(uint16_t))) == NULL)
		return ENOMEM;
	for (i = 0; i < 65536; i++)
		tab[i] = (uint16_t) i;
	/* Default ASCII fold, used if the table is missing/unreadable. */
	for (i = 'a'; i <= 'z'; i++)
		tab[i] = (uint16_t)(i - 'a' + 'A');

	pmp->pm_upcase = tab;

	len = pmp->pm_upcase_length;
	if (pmp->pm_upcase_cluster < EXFAT_CLUST_FIRST || len < 2 ||
	    len > 65536 * sizeof(uint16_t))
		return OK;	/* keep the ASCII-fold fallback */

	if ((raw = malloc((size_t) len)) == NULL)
		return OK;
	if (chain_read(pmp->pm_upcase_cluster, 0, 0, raw, (size_t) len) != OK) {
		free(raw);
		return OK;	/* keep the fallback */
	}

	/* Decompress: a value 0xFFFF is followed by a count of identity-mapped
	 * entries to skip; any other value sets the next table entry. */
	index = 0;
	i = 0;
	while ((uint64_t) i + 2 <= len && index < 65536) {
		uint16_t v = ex_get16(raw + i);
		i += 2;
		if (v == 0xFFFF) {
			if ((uint64_t) i + 2 > len)
				break;
			index += ex_get16(raw + i);	/* identity run */
			i += 2;
		} else {
			tab[index++] = v;
		}
	}

	free(raw);
	return OK;
}

/*===========================================================================*
 *				upcase_free				     *
 *===========================================================================*/
void upcase_free(void)
{
	if (pmp->pm_upcase != NULL) {
		free(pmp->pm_upcase);
		pmp->pm_upcase = NULL;
	}
}

/*===========================================================================*
 *				upcase_one				     *
 *===========================================================================*/
uint16_t upcase_one(uint16_t wc)
{
	if (pmp->pm_upcase == NULL)
		return (wc >= 'a' && wc <= 'z') ? (uint16_t)(wc - 'a' + 'A') : wc;
	return pmp->pm_upcase[wc];
}

/*===========================================================================*
 *				name_hash				     *
 *===========================================================================*/
uint16_t name_hash(const uint16_t *name, int len)
{
/* exFAT NameHash: a byte-wise rotate-and-add over the up-cased name, processed
 * little-endian (low byte then high byte of each code unit). */
	uint16_t hash = 0;
	int i;

	for (i = 0; i < len; i++) {
		uint16_t c = upcase_one(name[i]);

		hash = (uint16_t)(((hash << 15) | (hash >> 1)) + (c & 0xFF));
		hash = (uint16_t)(((hash << 15) | (hash >> 1)) + (c >> 8));
	}
	return hash;
}

/*===========================================================================*
 *				utf16_to_utf8				     *
 *===========================================================================*/
int utf16_to_utf8(const uint16_t *in, int inlen, char *out, size_t outsize)
{
/* Convert UTF-16 to a NUL-terminated UTF-8 string.  Returns the byte length
 * (excluding the NUL), or -1 if it would not fit. */
	size_t o = 0;
	int i;

	for (i = 0; i < inlen; i++) {
		uint32_t cp = in[i];

		/* Surrogate pair. */
		if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < inlen &&
		    in[i + 1] >= 0xDC00 && in[i + 1] <= 0xDFFF) {
			cp = 0x10000 + ((cp - 0xD800) << 10) +
			    (in[++i] - 0xDC00);
		}

		if (cp < 0x80) {
			if (o + 1 >= outsize) return -1;
			out[o++] = (char) cp;
		} else if (cp < 0x800) {
			if (o + 2 >= outsize) return -1;
			out[o++] = (char)(0xC0 | (cp >> 6));
			out[o++] = (char)(0x80 | (cp & 0x3F));
		} else if (cp < 0x10000) {
			if (o + 3 >= outsize) return -1;
			out[o++] = (char)(0xE0 | (cp >> 12));
			out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
			out[o++] = (char)(0x80 | (cp & 0x3F));
		} else {
			if (o + 4 >= outsize) return -1;
			out[o++] = (char)(0xF0 | (cp >> 18));
			out[o++] = (char)(0x80 | ((cp >> 12) & 0x3F));
			out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
			out[o++] = (char)(0x80 | (cp & 0x3F));
		}
	}
	out[o] = '\0';
	return (int) o;
}

/*===========================================================================*
 *				utf8_to_utf16				     *
 *===========================================================================*/
int utf8_to_utf16(const char *in, uint16_t *out, int outmax)
{
/* Convert a NUL-terminated UTF-8 string to UTF-16.  Returns the number of
 * code units, or -1 on overflow or malformed input. */
	const unsigned char *p = (const unsigned char *) in;
	int n = 0;

	while (*p != '\0') {
		uint32_t cp;
		int extra;

		if (*p < 0x80) {
			cp = *p++;
			extra = 0;
		} else if ((*p & 0xE0) == 0xC0) {
			cp = *p++ & 0x1F;
			extra = 1;
		} else if ((*p & 0xF0) == 0xE0) {
			cp = *p++ & 0x0F;
			extra = 2;
		} else if ((*p & 0xF8) == 0xF0) {
			cp = *p++ & 0x07;
			extra = 3;
		} else {
			return -1;
		}
		while (extra-- > 0) {
			if ((*p & 0xC0) != 0x80) return -1;
			cp = (cp << 6) | (*p++ & 0x3F);
		}

		if (cp < 0x10000) {
			if (n >= outmax) return -1;
			out[n++] = (uint16_t) cp;
		} else {
			if (n + 2 > outmax) return -1;
			cp -= 0x10000;
			out[n++] = (uint16_t)(0xD800 + (cp >> 10));
			out[n++] = (uint16_t)(0xDC00 + (cp & 0x3FF));
		}
	}
	return n;
}

/*===========================================================================*
 *				days_from_civil				     *
 *===========================================================================*/
static long days_from_civil(int y, unsigned m, unsigned d)
{
/* Days since 1970-01-01 for a proleptic-Gregorian date (Hinnant's algorithm). */
	long era;
	unsigned yoe, doy, doe;

	y -= (m <= 2);
	era = (long)((y >= 0 ? y : y - 399) / 400);
	yoe = (unsigned)(y - era * 400);
	doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
	doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
	return era * 146097L + (long) doe - 719468L;
}

/*===========================================================================*
 *				exfat_to_timespec			     *
 *===========================================================================*/
void exfat_to_timespec(uint32_t stamp, uint8_t inc10ms, uint8_t tzoff,
	struct timespec *tsp)
{
/* Convert an exFAT timestamp (DOS-style packed local time + 10ms increment +
 * UTC offset) into a UTC struct timespec. */
	unsigned sec = (stamp & 0x1F) * 2;
	unsigned min = (stamp >> 5) & 0x3F;
	unsigned hour = (stamp >> 11) & 0x1F;
	unsigned day = (stamp >> 16) & 0x1F;
	unsigned mon = (stamp >> 21) & 0x0F;
	unsigned year = ((stamp >> 25) & 0x7F) + 1980;
	long days;
	time_t secs;

	if (mon < 1) mon = 1;
	if (mon > 12) mon = 12;
	if (day < 1) day = 1;

	days = days_from_civil((int) year, mon, day);
	secs = ((time_t) days * 24 + hour) * 3600 + min * 60 + sec;

	/* The 10ms-increment field carries an extra whole second when >= 100. */
	if (inc10ms >= 100) {
		secs += 1;
		inc10ms = (uint8_t)(inc10ms - 100);
	}

	/* UTC offset: signed value in 15-minute units, valid when bit 7 is set. */
	if (tzoff & 0x80) {
		int off = (int)(int8_t) tzoff;	/* sign-extend the whole byte */
		secs -= (time_t) off * 15 * 60;
	}

	tsp->tv_sec = secs;
	tsp->tv_nsec = (long) inc10ms * 10000000L;
}
