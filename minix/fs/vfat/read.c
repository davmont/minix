/* File reading for the vfat server. */
#include "fs.h"
#include <sys/stat.h>
#include <sys/mman.h>		/* mmap, for fs_peek */
#include <minix/vm.h>		/* vm_set_cacheblock, for fs_peek */

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

/*===========================================================================*
 *				fs_peek					     *
 *===========================================================================*/
ssize_t fs_peek(ino_t ino_nr, struct fsdriver_data *__unused data, size_t bytes,
	off_t pos, int __unused call)
{
/* Assemble a full page of file data and hand it to VM's cache, so that VM can
 * back file mmap()s and demand-paged exec() from a FAT volume.  FAT data
 * clusters do not align to page-size device-block boundaries, so -- exactly
 * like isofs -- we read the requested range into a private anonymous page via
 * the normal read path (which walks the cluster chain) and register that page
 * with VM for one-time use, rather than trying to peek raw device blocks.
 */
	static u32_t flags = 0;	/* persistent storage for the VMMC_ flags */
	static off_t dev_off = 0; /* fake, ever-increasing device offset */
	struct fsdriver_data buf_data;
	char *buf;
	ssize_t r;

	if ((buf = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
	    MAP_ANON | MAP_PRIVATE, -1, 0)) == MAP_FAILED)
		return ENOMEM;

	/*
	 * Read the file data into our local page via the cluster-walking read
	 * path.  For the SELF endpoint, fsdriver_copyout() uses the union's
	 * 'ptr' member (a full pointer), not 'grant' (a 32-bit grant id that
	 * would truncate the pointer on amd64).
	 */
	buf_data.endpt = SELF;
	buf_data.ptr = buf;
	buf_data.size = bytes;

	r = fs_readwrite(ino_nr, &buf_data, bytes, pos, FSC_READ);

	if (r >= 0) {
		/* Zero the tail beyond EOF so VM gets a fully defined page. */
		if ((size_t)r < bytes)
			memset(&buf[r], 0, bytes - r);

		/*
		 * One-time page: the device offset is just a unique cache key
		 * that is discarded right after use; an ever-increasing value
		 * keeps it unique.
		 */
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
