/* crc32c (Castagnoli) checksum, used by ext4's metadata_csum feature.
 *
 * This is the reflected CRC-32C (polynomial 0x1EDC6F41, reflected 0x82F63B78)
 * implemented as a chained update: ext2_crc32c(crc, buf, len) continues the
 * running CRC from 'crc', performing no pre/post inversion.  Callers seed with
 * 0xFFFFFFFF where a fresh CRC is wanted (as Linux's crc32c() does) and the
 * stored value is the raw running CRC, truncated as each on-disk field needs.
 *
 * Created:
 *   June 2026 (ext4 metadata_csum support)
 */

#include "fs.h"
#include "proto.h"

static u32_t crc32c_table[256];
static int crc32c_ready = 0;

static void crc32c_init(void)
{
	u32_t c;
	int n, k;

	for (n = 0; n < 256; n++) {
		c = (u32_t) n;
		for (k = 0; k < 8; k++)
			c = (c & 1) ? (0x82F63B78 ^ (c >> 1)) : (c >> 1);
		crc32c_table[n] = c;
	}
	crc32c_ready = 1;
}

u32_t ext2_crc32c(u32_t crc, const void *buf, size_t len)
{
	const u8_t *p = buf;

	if (!crc32c_ready)
		crc32c_init();

	while (len--)
		crc = crc32c_table[(crc ^ *p++) & 0xff] ^ (crc >> 8);

	return crc;
}
