/* File reading for the vfat server. */
#include "fs.h"
#include <sys/stat.h>

/*===========================================================================*
 *				fs_readwrite				     *
 *===========================================================================*/
ssize_t fs_readwrite(ino_t ino_nr, struct fsdriver_data *data, size_t bytes,
	off_t pos, int call)
{
/* Read up to 'bytes' bytes from file ino_nr at offset 'pos'.  vfat is
 * read-only, so only FSC_READ is ever requested.
 */
	struct inode *rip;
	off_t fsize;
	size_t off, chunk, left;
	unsigned long frcn, cloff, bn, cn, sec, secoff;
	struct buf *bp;
	ssize_t total;
	int r;

	if (call != FSC_READ && call != FSC_WRITE)
		return EINVAL;

	if ((rip = find_inode(ino_nr)) == NULL)
		return EINVAL;

	if (rip->i_attrs & ATTR_DIRECTORY)
		return EISDIR;

	/* ----- write path ----- */
	if (call == FSC_WRITE) {
		off_t end = pos + (off_t) bytes;

		if (pmp->pm_rdonly)
			return EROFS;
		if (pos < 0)
			return EINVAL;
		if (bytes == 0)
			return 0;

		/* Allocate clusters to cover the new end of file. */
		if (end > (off_t) rip->i_size) {
			if ((r = extend_file(rip, (uint32_t) end)) != OK)
				return r;
		}

		left = bytes;
		off = 0;
		total = 0;
		while (left > 0) {
			frcn = (unsigned long) (pos / pmp->pm_bpcluster);
			cloff = (unsigned long) (pos % pmp->pm_bpcluster);

			if ((r = bmap(rip, frcn, &bn, &cn, NULL)) != OK)
				return (total > 0) ? total : r;
			if (bn == 0)
				break;	/* should not happen after extend */

			sec = bn + cloff / pmp->pm_BytesPerSec;
			secoff = cloff % pmp->pm_BytesPerSec;
			chunk = pmp->pm_BytesPerSec - secoff;
			if (chunk > left)
				chunk = left;

			/* Read-modify-write the sector (partial writes). */
			if ((bp = get_block(pmp->pm_dev, sec, NORMAL)) == NULL)
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

		if (pos > (off_t) rip->i_size)
			rip->i_size = (uint32_t) pos;
		rip->i_mtime = vfat_now();
		if ((r = update_direntry(rip)) != OK)
			return (total > 0) ? total : r;

		return total;
	}

	/* ----- read path ----- */
	fsize = (off_t) rip->i_size;
	if (pos < 0)
		return EINVAL;
	if (pos >= fsize)
		return 0;		/* at or past EOF */

	left = bytes;
	if ((off_t) left > fsize - pos)
		left = (size_t) (fsize - pos);

	total = 0;
	off = 0;

	while (left > 0) {
		frcn = (unsigned long) (pos / pmp->pm_bpcluster);
		cloff = (unsigned long) (pos % pmp->pm_bpcluster);

		if ((r = bmap(rip, frcn, &bn, &cn, NULL)) != OK)
			return (total > 0) ? total : r;
		if (bn == 0)
			break;		/* unexpected hole; stop */

		sec = bn + cloff / pmp->pm_BytesPerSec;
		secoff = cloff % pmp->pm_BytesPerSec;

		chunk = pmp->pm_BytesPerSec - secoff;
		if (chunk > left)
			chunk = left;

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
