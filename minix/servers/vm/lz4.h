#ifndef _VM_LZ4_H
#define _VM_LZ4_H 1

/* Minimal LZ4 block-format codec for the VM compressed-memory store
 * (RECLAIM_DESIGN.md, phase B).  Not a general-purpose library: the
 * compressor gives up early when the output would exceed the caller's
 * budget (incompressible input is simply not stored), and both
 * directions are only used on VM-internal buffers.
 */

/* Compress src[0..srclen) into dst[0..dstcap).  Returns the compressed
 * size, or 0 if the result would not fit in dstcap (caller should then
 * keep the page uncompressed).
 */
int vm_lz4_compress(const unsigned char *src, int srclen,
	unsigned char *dst, int dstcap);

/* Decompress src[0..srclen) into dst[0..dstcap).  Returns the number
 * of bytes produced, or -1 on corrupt input / overflow.  Never writes
 * outside dst[0..dstcap).
 */
int vm_lz4_decompress(const unsigned char *src, int srclen,
	unsigned char *dst, int dstcap);

/* One-time self-test (round-trips a few characteristic buffers).
 * Returns 0 on success, -1 on failure.
 */
int vm_lz4_selftest(void);

#endif
