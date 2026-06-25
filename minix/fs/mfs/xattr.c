#include "fs.h"
#include <string.h>
#include <sys/stat.h>
#include <sys/extattr.h>
#include "buf.h"
#include "inode.h"
#include "super.h"
#include "xattr.h"

/* Linux setxattr flags; BSD extattr passes 0.  Defined here for the future
 * Linux-compatibility shim (honoured already so it needs no FS change). */
#ifndef XATTR_CREATE
#define XATTR_CREATE	0x01	/* set value, fail if attr already exists */
#define XATTR_REPLACE	0x02	/* set value, fail if attr does not exist */
#endif

/*===========================================================================*
 *				xattr_ns_ok				     *
 *===========================================================================*/
static int xattr_ns_ok(int ns)
{
/* Only the user and system namespaces are understood. */
  return ns == EXTATTR_NAMESPACE_USER || ns == EXTATTR_NAMESPACE_SYSTEM;
}

/*===========================================================================*
 *				xattr_getbuf				     *
 *===========================================================================*/
static struct buf *xattr_getbuf(struct inode *rip, int how)
{
/* Return the buffer holding 'rip's extended-attribute block, or NULL if the
 * inode has none.  'how' is NORMAL (read it in) or NO_READ (caller will
 * initialise a freshly allocated block). */
  block_t b;

  if (rip->i_xattr_zone == NO_ZONE) return(NULL);
  b = (block_t) rip->i_xattr_zone << rip->i_sp->s_log_zone_size;
  return get_block(rip->i_dev, b, how);
}

/*===========================================================================*
 *				xattr_walk				     *
 *===========================================================================*/
static int xattr_walk(char *blk, size_t bsize, unsigned *count, size_t *used)
{
/* Validate the xattr block header and walk its entries, returning the entry
 * count and the number of bytes occupied (header + all entries).  Returns OK,
 * or EINVAL if the block is not a valid xattr block. */
  struct mfs_xattr_hdr *xh = (struct mfs_xattr_hdr *) blk;
  size_t off;
  unsigned i, n;

  if (xh->xh_magic != MFS_XATTR_MAGIC) return(EINVAL);
  n = xh->xh_count;
  off = sizeof(struct mfs_xattr_hdr);
  for (i = 0; i < n; i++) {
	struct mfs_xattr_ent *xe = (struct mfs_xattr_ent *) (blk + off);
	size_t esz;
	if (off + sizeof(*xe) > bsize) return(EINVAL);
	esz = MFS_XATTR_ENTSIZE(xe->xe_namelen, xe->xe_vallen);
	if (xe->xe_namelen == 0 || off + esz > bsize) return(EINVAL);
	off += esz;
  }
  if (count != NULL) *count = n;
  if (used != NULL) *used = off;
  return(OK);
}

/*===========================================================================*
 *				xattr_find				     *
 *===========================================================================*/
static struct mfs_xattr_ent *xattr_find(char *blk, unsigned count, int ns,
	const char *name, size_t namelen)
{
/* Locate the entry for (ns, name) within a validated block, or NULL. */
  size_t off = sizeof(struct mfs_xattr_hdr);
  unsigned i;

  for (i = 0; i < count; i++) {
	struct mfs_xattr_ent *xe = (struct mfs_xattr_ent *) (blk + off);
	if (xe->xe_ns == ns && xe->xe_namelen == namelen &&
	    memcmp(blk + off + sizeof(*xe), name, namelen) == 0)
		return(xe);
	off += MFS_XATTR_ENTSIZE(xe->xe_namelen, xe->xe_vallen);
  }
  return(NULL);
}

/*===========================================================================*
 *				fs_getxattr				     *
 *===========================================================================*/
ssize_t fs_getxattr(ino_t ino_nr, int attrnamespace, const char *name,
	struct fsdriver_data *data, size_t bytes)
{
/* Return the value of one extended attribute (or, if 'bytes' is 0, its size). */
  struct inode *rip;
  struct buf *bp;
  struct mfs_xattr_ent *xe;
  unsigned count;
  size_t namelen;
  ssize_t r;

  if (!xattr_ns_ok(attrnamespace)) return(EINVAL);
  if ((rip = find_inode(fs_dev, ino_nr)) == NULL) return(EINVAL);
  namelen = strlen(name);
  if (namelen == 0 || namelen > MFS_XATTR_NAME_MAX) return(EINVAL);

  if ((bp = xattr_getbuf(rip, NORMAL)) == NULL) return(ENOATTR);
  if (xattr_walk(b_data(bp), rip->i_sp->s_block_size, &count, NULL) != OK) {
	put_block(bp);
	return(ENOATTR);
  }
  if ((xe = xattr_find(b_data(bp), count, attrnamespace, name, namelen))
      == NULL) {
	put_block(bp);
	return(ENOATTR);
  }

  if (bytes == 0)			/* size query */
	r = (ssize_t) xe->xe_vallen;
  else if (bytes < xe->xe_vallen)	/* buffer too small */
	r = ERANGE;
  else if (xe->xe_vallen == 0)		/* empty value: nothing to copy */
	r = 0;
  else {
	char *val = (char *) xe + sizeof(*xe) + xe->xe_namelen;
	if ((r = fsdriver_copyout(data, 0, val, xe->xe_vallen)) == OK)
		r = (ssize_t) xe->xe_vallen;
  }

  put_block(bp);
  return(r);
}

/*===========================================================================*
 *				fs_listxattr				     *
 *===========================================================================*/
ssize_t fs_listxattr(ino_t ino_nr, int attrnamespace,
	struct fsdriver_data *data, size_t bytes)
{
/* Return the names of all attributes in 'attrnamespace', each as a one-byte
 * length followed by the name (the EXTATTR_LIST_LENPREFIX format), or just the
 * total size if 'bytes' is 0. */
  struct inode *rip;
  struct buf *bp;
  char *blk;
  unsigned count, i;
  size_t off, total;
  ssize_t r = OK;

  if (!xattr_ns_ok(attrnamespace)) return(EINVAL);
  if ((rip = find_inode(fs_dev, ino_nr)) == NULL) return(EINVAL);

  if ((bp = xattr_getbuf(rip, NORMAL)) == NULL) return(0);
  blk = b_data(bp);
  if (xattr_walk(blk, rip->i_sp->s_block_size, &count, NULL) != OK) {
	put_block(bp);
	return(0);
  }

  /* First pass: total size of the name list. */
  total = 0;
  off = sizeof(struct mfs_xattr_hdr);
  for (i = 0; i < count; i++) {
	struct mfs_xattr_ent *xe = (struct mfs_xattr_ent *) (blk + off);
	if (xe->xe_ns == attrnamespace) total += 1 + xe->xe_namelen;
	off += MFS_XATTR_ENTSIZE(xe->xe_namelen, xe->xe_vallen);
  }

  if (bytes == 0) {			/* size query */
	put_block(bp);
	return((ssize_t) total);
  }
  if (bytes < total) {
	put_block(bp);
	return(ERANGE);
  }

  /* Second pass: emit each matching name with its length prefix. */
  total = 0;
  off = sizeof(struct mfs_xattr_hdr);
  for (i = 0; i < count; i++) {
	struct mfs_xattr_ent *xe = (struct mfs_xattr_ent *) (blk + off);
	if (xe->xe_ns == attrnamespace) {
		char ent[1 + MFS_XATTR_NAME_MAX];
		ent[0] = xe->xe_namelen;
		memcpy(ent + 1, (char *) xe + sizeof(*xe), xe->xe_namelen);
		if ((r = fsdriver_copyout(data, total, ent,
		    1 + xe->xe_namelen)) != OK)
			break;
		total += 1 + xe->xe_namelen;
	}
	off += MFS_XATTR_ENTSIZE(xe->xe_namelen, xe->xe_vallen);
  }

  put_block(bp);
  return(r == OK ? (ssize_t) total : r);
}

/*===========================================================================*
 *				fs_setxattr				     *
 *===========================================================================*/
int fs_setxattr(ino_t ino_nr, int attrnamespace, const char *name,
	struct fsdriver_data *data, size_t bytes, int flags)
{
/* Create or replace one extended attribute. */
  struct inode *rip;
  struct buf *bp;
  struct mfs_xattr_ent *xe, *old;
  char *blk;
  unsigned count;
  size_t namelen, bsize, used, esz, off;
  int newzone = 0, r;
  zone_t z;

  if (!xattr_ns_ok(attrnamespace)) return(EINVAL);
  if ((rip = find_inode(fs_dev, ino_nr)) == NULL) return(EINVAL);
  if (rip->i_sp->s_rd_only) return(EROFS);
  /* An immutable or append-only file's attributes may not be changed (V4). */
  if (rip->i_flags & (UF_IMMUTABLE | SF_IMMUTABLE | UF_APPEND | SF_APPEND))
	return(EPERM);
  namelen = strlen(name);
  if (namelen == 0 || namelen > MFS_XATTR_NAME_MAX) return(EINVAL);
  if (bytes > MFS_XATTR_VAL_MAX) return(E2BIG);

  bsize = rip->i_sp->s_block_size;

  /* Obtain the xattr block, allocating and initialising one if needed. */
  if (rip->i_xattr_zone == NO_ZONE) {
	if (flags & XATTR_REPLACE) return(ENOATTR);
	if ((z = alloc_zone(rip->i_dev,
	    (zone_t) rip->i_sp->s_firstdatazone)) == NO_ZONE)
		return(err_code);
	rip->i_xattr_zone = (u32_t) z;
	newzone = 1;
	bp = get_block(rip->i_dev,
	    (block_t) z << rip->i_sp->s_log_zone_size, NO_READ);
	blk = b_data(bp);
	memset(blk, 0, bsize);
	((struct mfs_xattr_hdr *) blk)->xh_magic = MFS_XATTR_MAGIC;
	((struct mfs_xattr_hdr *) blk)->xh_count = 0;
  } else {
	bp = xattr_getbuf(rip, NORMAL);
	blk = b_data(bp);
	if (xattr_walk(blk, bsize, NULL, NULL) != OK) {
		/* Corrupt block: reinitialise it rather than propagate. */
		memset(blk, 0, bsize);
		((struct mfs_xattr_hdr *) blk)->xh_magic = MFS_XATTR_MAGIC;
		((struct mfs_xattr_hdr *) blk)->xh_count = 0;
	}
  }

  (void) xattr_walk(blk, bsize, &count, &used);
  old = xattr_find(blk, count, attrnamespace, name, namelen);

  if ((flags & XATTR_CREATE) && old != NULL) { r = EEXIST; goto out; }
  if ((flags & XATTR_REPLACE) && old == NULL) { r = ENOATTR; goto out; }

  /* Remove any existing entry (we always append the new value at the end). */
  if (old != NULL) {
	off = (size_t) ((char *) old - blk);
	esz = MFS_XATTR_ENTSIZE(old->xe_namelen, old->xe_vallen);
	memmove(blk + off, blk + off + esz, used - off - esz);
	used -= esz;
	memset(blk + used, 0, esz);
	((struct mfs_xattr_hdr *) blk)->xh_count = --count;
  }

  /* Append the new entry, copying the value straight into the block. */
  esz = MFS_XATTR_ENTSIZE(namelen, bytes);
  if (used + esz > bsize) { r = ENOSPC; goto out; }
  xe = (struct mfs_xattr_ent *) (blk + used);
  xe->xe_ns = (u8_t) attrnamespace;
  xe->xe_namelen = (u8_t) namelen;
  xe->xe_vallen = (u16_t) bytes;
  memcpy(blk + used + sizeof(*xe), name, namelen);
  if (bytes > 0 &&
      (r = fsdriver_copyin(data, 0, blk + used + sizeof(*xe) + namelen,
       bytes)) != OK)
	goto out;
  /* zero the alignment padding for a stable on-disk image */
  memset(blk + used + sizeof(*xe) + namelen + bytes, 0,
	esz - sizeof(*xe) - namelen - bytes);
  ((struct mfs_xattr_hdr *) blk)->xh_count = count + 1;

  MARKDIRTY(bp);
  put_block(bp);

  if (newzone) IN_MARKDIRTY(rip);	/* d4_xattr_zone changed */

  /* Lazily record that this filesystem now uses the xattr feature, so an older
   * driver (which would clobber d4_xattr_zone) refuses to mount it. */
  if (!(superblock.s_feature_incompat & MFS_INCOMPAT_XATTR_BLOCK)) {
	superblock.s_feature_incompat |= MFS_INCOMPAT_XATTR_BLOCK;
	(void) write_super(&superblock);
  }
  return(OK);

out:
  put_block(bp);
  if (newzone) {			/* roll back the just-allocated zone */
	free_zone(rip->i_dev, (zone_t) rip->i_xattr_zone);
	rip->i_xattr_zone = NO_ZONE;
  }
  return(r);
}

/*===========================================================================*
 *				fs_removexattr				     *
 *===========================================================================*/
int fs_removexattr(ino_t ino_nr, int attrnamespace, const char *name)
{
/* Delete one extended attribute.  Frees the block if it becomes empty. */
  struct inode *rip;
  struct buf *bp;
  struct mfs_xattr_ent *old;
  char *blk;
  unsigned count;
  size_t namelen, used, esz, off;

  if (!xattr_ns_ok(attrnamespace)) return(EINVAL);
  if ((rip = find_inode(fs_dev, ino_nr)) == NULL) return(EINVAL);
  if (rip->i_sp->s_rd_only) return(EROFS);
  /* An immutable or append-only file's attributes may not be removed (V4). */
  if (rip->i_flags & (UF_IMMUTABLE | SF_IMMUTABLE | UF_APPEND | SF_APPEND))
	return(EPERM);
  namelen = strlen(name);
  if (namelen == 0 || namelen > MFS_XATTR_NAME_MAX) return(EINVAL);

  if ((bp = xattr_getbuf(rip, NORMAL)) == NULL) return(ENOATTR);
  blk = b_data(bp);
  if (xattr_walk(blk, rip->i_sp->s_block_size, &count, &used) != OK) {
	put_block(bp);
	return(ENOATTR);
  }
  if ((old = xattr_find(blk, count, attrnamespace, name, namelen)) == NULL) {
	put_block(bp);
	return(ENOATTR);
  }

  off = (size_t) ((char *) old - blk);
  esz = MFS_XATTR_ENTSIZE(old->xe_namelen, old->xe_vallen);
  memmove(blk + off, blk + off + esz, used - off - esz);
  used -= esz;
  memset(blk + used, 0, esz);
  ((struct mfs_xattr_hdr *) blk)->xh_count = --count;

  if (count == 0) {
	/* No attributes left: release the block. */
	zone_t z = (zone_t) rip->i_xattr_zone;
	put_block(bp);
	free_zone(rip->i_dev, z);
	rip->i_xattr_zone = NO_ZONE;
	IN_MARKDIRTY(rip);
  } else {
	MARKDIRTY(bp);
	put_block(bp);
  }
  return(OK);
}
