/* Minimal LZ4 block-format codec for the VM compressed-memory store
 * (RECLAIM_DESIGN.md, phase B).  Implements the standard LZ4 block
 * format (so it could interoperate with other LZ4 tools), with a
 * greedy hash-chain-less matcher tuned for 4 KB pages:
 *
 *   sequence = token (4b literal len | 4b match len)
 *              [literal length extension bytes]
 *              literals
 *              little-endian 16-bit match offset
 *              [match length extension bytes]
 *
 * Format rules honored: minimum match 4 bytes (encoded match length is
 * length-4); the block ends with a literals-only sequence; the last 5
 * bytes are always literals; no match starts within the last 12 bytes.
 *
 * The compressor bails out (returns 0) as soon as the output would
 * exceed the caller's budget: for the VM store, an incompressible page
 * is simply kept uncompressed, so speed on such pages matters more
 * than trying hard.
 */

#include <string.h>

#include "lz4.h"

#define LZ4_MINMATCH	4
#define LZ4_MFLIMIT	12	/* no matches within last 12 bytes */
#define LZ4_LASTLITERALS 5	/* last 5 bytes must be literals */
#define LZ4_MAXOFFSET	65535

#define HASH_LOG	12
#define HASH_SIZE	(1 << HASH_LOG)

static unsigned int
lz4_hash4(const unsigned char *p)
{
	unsigned int v = (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
	    ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);

	return (v * 2654435761u) >> (32 - HASH_LOG);
}

int
vm_lz4_compress(const unsigned char *src, int srclen,
	unsigned char *dst, int dstcap)
{
	unsigned short htab[HASH_SIZE];	/* offsets into src, +1 (0 = empty) */
	const unsigned char *ip = src, *iend = src + srclen;
	const unsigned char *anchor = src;
	const unsigned char *mflimit;
	unsigned char *op = dst, *oend = dst + dstcap;
	int i;

	if (srclen <= 0 || srclen > 65535 || dstcap <= 0)
		return 0;

	mflimit = iend - LZ4_MFLIMIT;

	memset(htab, 0, sizeof(htab));

	if (srclen >= LZ4_MFLIMIT) {
		while (ip < mflimit) {
			const unsigned char *match = NULL;
			unsigned int h;
			int litlen, mlen, token_mlen;
			unsigned char *token;

			/* Find a match for the 4 bytes at ip. */
			h = lz4_hash4(ip);
			if (htab[h] != 0) {
				const unsigned char *cand = src + htab[h] - 1;

				if (ip - cand <= LZ4_MAXOFFSET &&
				    cand[0] == ip[0] && cand[1] == ip[1] &&
				    cand[2] == ip[2] && cand[3] == ip[3])
					match = cand;
			}
			htab[h] = (unsigned short)(ip - src + 1);

			if (match == NULL) {
				ip++;
				continue;
			}

			/* Extend the match forward (not past mflimit+8;
			 * stop so the last-5-literals rule can hold). */
			mlen = LZ4_MINMATCH;
			while (ip + mlen < iend - LZ4_LASTLITERALS &&
			    match[mlen] == ip[mlen])
				mlen++;

			/* Emit sequence: literals since anchor + match. */
			litlen = ip - anchor;

			/* Worst-case space: token + litlen ext + literals +
			 * offset + mlen ext.  Bail out if it can't fit. */
			if (op + 1 + (litlen / 255 + 1) + litlen + 2 +
			    ((mlen - LZ4_MINMATCH) / 255 + 1) +
			    LZ4_LASTLITERALS >= oend)
				return 0;

			token = op++;
			if (litlen >= 15) {
				int l = litlen - 15;

				*token = 15 << 4;
				while (l >= 255) {
					*op++ = 255;
					l -= 255;
				}
				*op++ = (unsigned char)l;
			} else
				*token = (unsigned char)(litlen << 4);

			for (i = 0; i < litlen; i++)
				*op++ = anchor[i];

			*op++ = (unsigned char)((ip - match) & 0xff);
			*op++ = (unsigned char)(((ip - match) >> 8) & 0xff);

			token_mlen = mlen - LZ4_MINMATCH;
			if (token_mlen >= 15) {
				int l = token_mlen - 15;

				*token |= 15;
				while (l >= 255) {
					*op++ = 255;
					l -= 255;
				}
				*op++ = (unsigned char)l;
			} else
				*token |= (unsigned char)token_mlen;

			ip += mlen;
			anchor = ip;
		}
	}

	/* Trailing literals (always at least LZ4_LASTLITERALS if any
	 * match was emitted; possibly the whole input). */
	{
		int litlen = iend - anchor;

		if (op + 1 + (litlen / 255 + 1) + litlen > oend)
			return 0;

		if (litlen >= 15) {
			int l = litlen - 15;

			*op++ = 15 << 4;
			while (l >= 255) {
				*op++ = 255;
				l -= 255;
			}
			*op++ = (unsigned char)l;
		} else
			*op++ = (unsigned char)(litlen << 4);

		for (i = 0; i < litlen; i++)
			*op++ = anchor[i];
	}

	return op - dst;
}

int
vm_lz4_decompress(const unsigned char *src, int srclen,
	unsigned char *dst, int dstcap)
{
	const unsigned char *ip = src, *iend = src + srclen;
	unsigned char *op = dst, *oend = dst + dstcap;

	if (srclen <= 0)
		return -1;

	for (;;) {
		unsigned int token, litlen, mlen, offset;
		const unsigned char *cp;

		if (ip >= iend)
			return -1;
		token = *ip++;

		/* Literals. */
		litlen = token >> 4;
		if (litlen == 15) {
			unsigned int b;

			do {
				if (ip >= iend)
					return -1;
				b = *ip++;
				litlen += b;
			} while (b == 255);
		}
		if ((unsigned int)(iend - ip) < litlen ||
		    (unsigned int)(oend - op) < litlen)
			return -1;
		memcpy(op, ip, litlen);
		ip += litlen;
		op += litlen;

		if (ip == iend)
			break;	/* end of block: literals-only sequence */

		/* Match. */
		if (iend - ip < 2)
			return -1;
		offset = (unsigned int)ip[0] | ((unsigned int)ip[1] << 8);
		ip += 2;
		if (offset == 0 || (unsigned int)(op - dst) < offset)
			return -1;

		mlen = token & 15;
		if (mlen == 15) {
			unsigned int b;

			do {
				if (ip >= iend)
					return -1;
				b = *ip++;
				mlen += b;
			} while (b == 255);
		}
		mlen += LZ4_MINMATCH;
		if ((unsigned int)(oend - op) < mlen)
			return -1;

		/* Byte-by-byte: overlapping matches (offset < mlen) are
		 * valid LZ4 and must replicate. */
		cp = op - offset;
		while (mlen-- > 0)
			*op++ = *cp++;
	}

	return op - dst;
}

int
vm_lz4_selftest(void)
{
	static unsigned char in[4096], comp[4096], out[4096];
	unsigned int seed = 12345;
	int i, t, clen, dlen;

	for (t = 0; t < 4; t++) {
		int expect_compressible = 1;

		switch (t) {
		case 0:	/* zero page */
			memset(in, 0, sizeof(in));
			break;
		case 1:	/* repeating pattern */
			for (i = 0; i < (int)sizeof(in); i++)
				in[i] = (unsigned char)(i % 23);
			break;
		case 2:	/* text-like: limited alphabet, some repeats */
			for (i = 0; i < (int)sizeof(in); i++)
				in[i] = "the quick brown fox "[i % 20];
			break;
		case 3:	/* pseudo-random: incompressible */
			for (i = 0; i < (int)sizeof(in); i++) {
				seed = seed * 1103515245 + 12345;
				in[i] = (unsigned char)(seed >> 16);
			}
			expect_compressible = 0;
			break;
		}

		clen = vm_lz4_compress(in, sizeof(in), comp, sizeof(comp) / 2);

		if (!expect_compressible) {
			/* Must give up cleanly within half-page budget. */
			if (clen != 0)
				return -1;
			continue;
		}

		if (clen <= 0 || clen >= (int)sizeof(in) / 2)
			return -1;

		memset(out, 0xAA, sizeof(out));
		dlen = vm_lz4_decompress(comp, clen, out, sizeof(out));
		if (dlen != (int)sizeof(in))
			return -1;
		if (memcmp(in, out, sizeof(in)) != 0)
			return -1;
	}

	/* Corrupt-input robustness: decoder must fail, not crash. */
	for (i = 0; i < (int)sizeof(comp); i++)
		comp[i] = (unsigned char)(i * 7 + 3);
	(void)vm_lz4_decompress(comp, 64, out, sizeof(out));

	return 0;
}
