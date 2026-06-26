/* File reading for the exFAT server. */
#include "fs.h"
#include <sys/stat.h>
#include <sys/mman.h>		/* mmap, for fs_peek */
#include <minix/vm.h>		/* vm_set_cacheblock, for fs_peek */

/* A page of zeros, used to satisfy reads beyond ValidDataLength. */
static char zeroes[4096];

/*===========================================================================*
 *				fs_readwrite				     *
 *===========================================================================*/
ssize_t fs_readwrite(ino_t ino_nr, struct fsdriver_data *data, size_t bytes,
	off_t pos, int call)
{
/* Read up to 'bytes' bytes from file ino_nr at offset 'pos'.  exFAT is
 * read-only for now, so only FSC_READ is requested.  Bytes between the file's
 * ValidDataLength and its DataLength are defined to read as zero. */
	struct inode *rip;
	off_t fsize;
	size_t off, chunk, left;
	uint64_t frcn, cloff, sec;
	unsigned secoff;
	struct buf *bp;
	ssize_t total;
	int r;

	if (call != FSC_READ && call != FSC_WRITE)
		return EINVAL;
	if ((rip = find_inode(ino_nr)) == NULL)
		return EINVAL;
	if (rip->i_attrs & EXFAT_ATTR_DIRECTORY)
		return EISDIR;

	/* ----- write path ----- */
	if (call == FSC_WRITE) {
		off_t fend = pos + (off_t) bytes;

		if (pmp->pm_rdonly)
			return EROFS;
		if (pos < 0)
			return EINVAL;
		if (bytes == 0)
			return 0;

		if (fend > (off_t) rip->i_size) {
			if ((r = extend_file(rip, (uint64_t) fend)) != OK)
				return r;
			rip->i_size = (uint64_t) fend;
		}

		left = bytes;
		off = 0;
		total = 0;
		while (left > 0) {
			uint64_t wfrcn = (uint64_t) pos >> pmp->pm_clus_byte_shift;
			uint64_t wcloff = (uint64_t) pos &
			    (pmp->pm_bytes_per_clus - 1);
			uint64_t wsec;

			if ((r = bmap(rip, wfrcn, &wsec)) != OK)
				return (total > 0) ? total : r;
			wsec += wcloff >> pmp->pm_sec_shift;
			secoff = (unsigned)(wcloff & (pmp->pm_bytes_per_sec - 1));
			chunk = pmp->pm_bytes_per_sec - secoff;
			if (chunk > left)
				chunk = left;

			if ((bp = get_block(pmp->pm_dev, wsec, NORMAL)) == NULL)
				return (total > 0) ? total : EIO;
			r = fsdriver_copyin(data, off, b_data(bp) + secoff,
			    chunk);
			if (r == OK)
				lmfs_markdirty(bp);
			put_block(bp);
			if (r != OK)
				return (total > 0) ? total : r;

			pos += chunk;
			off += chunk;
			left -= chunk;
			total += chunk;
		}

		/* Everything written is now valid data. */
		if ((uint64_t) pos > rip->i_valid)
			rip->i_valid = (uint64_t) pos;
		rip->i_mtime = exfat_now();
		if ((r = update_direntry(rip)) != OK)
			return (total > 0) ? total : r;

		return total;
	}

	/* ----- read path ----- */
	fsize = (off_t) rip->i_size;
	if (pos < 0)
		return EINVAL;
	if (pos >= fsize)
		return 0;

	left = bytes;
	if ((off_t) left > fsize - pos)
		left = (size_t)(fsize - pos);

	total = 0;
	off = 0;

	while (left > 0) {
		if ((uint64_t) pos >= rip->i_valid) {
			/* Past ValidDataLength: return zeros without I/O. */
			chunk = left;
			if (chunk > sizeof(zeroes))
				chunk = sizeof(zeroes);
			r = fsdriver_copyout(data, off, zeroes, chunk);
			if (r != OK)
				return (total > 0) ? total : r;
			pos += chunk;
			off += chunk;
			left -= chunk;
			total += chunk;
			continue;
		}

		frcn = (uint64_t) pos >> pmp->pm_clus_byte_shift;
		cloff = (uint64_t) pos & (pmp->pm_bytes_per_clus - 1);

		if ((r = bmap(rip, frcn, &sec)) != OK)
			return (total > 0) ? total : (r == ENOENT ? 0 : r);

		sec += cloff >> pmp->pm_sec_shift;
		secoff = (unsigned)(cloff & (pmp->pm_bytes_per_sec - 1));

		chunk = pmp->pm_bytes_per_sec - secoff;
		if (chunk > left)
			chunk = left;
		/* Do not read past ValidDataLength in one go. */
		if ((uint64_t) pos + chunk > rip->i_valid)
			chunk = (size_t)(rip->i_valid - (uint64_t) pos);

		if ((bp = get_block(pmp->pm_dev, sec, NORMAL)) == NULL)
			return (total > 0) ? total : EIO;
		r = fsdriver_copyout(data, off, b_data(bp) + secoff, chunk);
		put_block(bp);
		if (r != OK)
			return (total > 0) ? total : r;

		pos += chunk;
		off += chunk;
		left -= chunk;
		total += chunk;
	}

	return total;
}

/*===========================================================================*
 *				fs_peek					     *
 *===========================================================================*/
ssize_t fs_peek(ino_t ino_nr, struct fsdriver_data *__unused data, size_t bytes,
	off_t pos, int __unused call)
{
/* Assemble a full page of file data and hand it to VM's cache, so VM can back
 * file mmap() and demand-paged exec() from an exFAT volume.  Like vfat/isofs,
 * we read the range into a private anonymous page via the normal cluster-walk
 * read path and register that page with VM for one-time use; this sidesteps
 * the fact that exFAT clusters need not align to page-size block boundaries. */
	static u32_t flags = 0;
	static off_t dev_off = 0;
	struct fsdriver_data buf_data;
	char *buf;
	ssize_t r;

	if ((buf = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
	    MAP_ANON | MAP_PRIVATE, -1, 0)) == MAP_FAILED)
		return ENOMEM;

	buf_data.endpt = SELF;
	buf_data.ptr = buf;
	buf_data.size = bytes;

	r = fs_readwrite(ino_nr, &buf_data, bytes, pos, FSC_READ);

	if (r >= 0) {
		if ((size_t) r < bytes)
			memset(&buf[r], 0, bytes - r);

		r = vm_set_cacheblock(buf, pmp->pm_dev, dev_off, ino_nr, pos,
		    &flags, bytes, VMSF_ONCE);

		if (r == OK) {
			dev_off += bytes;
			r = bytes;
		}
	}

	munmap(buf, bytes);

	return r;
}
