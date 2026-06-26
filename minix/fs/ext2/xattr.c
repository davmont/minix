/* Extended-attribute support for the ext2/ext3/ext4 on-disk format.
 *
 * ext4 stores a file's extended attributes in two places, both using the same
 * entry format:
 *   - "in-inode": in the space past the fixed inode (at 128 + i_extra_isize),
 *     introduced by a 4-byte magic, when the inode is larger than 128 bytes;
 *   - "in-block": a single block pointed at by i_file_acl, introduced by a
 *     32-byte header (shareable between inodes via a reference count).
 * Each attribute is a fixed entry header followed by its name; the values are
 * packed from the end of the region growing down, located by e_value_offs.
 *
 * The MINIX VFS speaks the BSD extattr interface: a (namespace, name) pair in
 * the USER or SYSTEM namespace.  ext4 instead encodes the namespace as a small
 * integer e_name_index prefixing the on-disk name, so the two are mapped onto
 * each other here.  In particular POSIX.1e ACLs live under the SYSTEM namespace
 * as "posix_acl_access"/"posix_acl_default", which ext4 stores as the dedicated
 * indices 2 and 3 -- so getfacl(1) and the VFS permission checks work unchanged.
 *
 * This file currently implements the read side (get/list); set/remove are not
 * yet wired up.
 *
 * Created:
 *   June 2026 (ext4 xattr support)
 */

#include "fs.h"
#include <string.h>
#include <sys/extattr.h>
#include <sys/acl.h>
#include "buf.h"
#include "inode.h"
#include "super.h"

/* On-disk magic shared by the in-inode and in-block attribute regions. */
#define EXT2_XATTR_MAGIC	0xEA020000

/* e_name_index namespace codes. */
#define EXT4_XATTR_INDEX_USER			1
#define EXT4_XATTR_INDEX_POSIX_ACL_ACCESS	2
#define EXT4_XATTR_INDEX_POSIX_ACL_DEFAULT	3
#define EXT4_XATTR_INDEX_TRUSTED		4
#define EXT4_XATTR_INDEX_SECURITY		6
#define EXT4_XATTR_INDEX_SYSTEM			7

/* Inode field offsets used to find the in-inode region. */
#define INO_EXTRA_ISIZE_OFF	0x80

#define XATTR_NAME_MAX		255

/* On-disk structures (little-endian; amd64 is LE so they map directly). */
struct ext2_xattr_block_header {
	u32_t h_magic;
	u32_t h_refcount;
	u32_t h_blocks;
	u32_t h_hash;
	u32_t h_checksum;
	u32_t h_reserved[3];
};
#define XATTR_BLOCK_HDR_SIZE	32

struct ext2_xattr_entry {
	u8_t  e_name_len;
	u8_t  e_name_index;
	u16_t e_value_offs;	/* value offset within its region */
	u32_t e_value_inum;	/* nonzero: value held in a separate inode */
	u32_t e_value_size;
	u32_t e_hash;
	/* followed by char e_name[e_name_len] */
};
#define XATTR_ENT_SIZE		12	/* fixed part, before the name */
#define XATTR_ENT_PAD(namelen)	((XATTR_ENT_SIZE + (namelen) + 3) & ~3U)

/*===========================================================================*
 *				xattr_ns_ok				     *
 *===========================================================================*/
static int xattr_ns_ok(int ns)
{
	return ns == EXTATTR_NAMESPACE_USER || ns == EXTATTR_NAMESPACE_SYSTEM;
}

/*===========================================================================*
 *				map_to_ext4				     *
 *===========================================================================*/
static int map_to_ext4(int ns, const char *name, u8_t *index,
	const char **e4name)
{
/* Translate a (BSD namespace, name) request into the ext4 (index, name) it is
 * stored under.  Returns FALSE for a request that ext4 cannot represent. */
	if (ns == EXTATTR_NAMESPACE_USER) {
		*index = EXT4_XATTR_INDEX_USER;
		*e4name = name;
		return TRUE;
	}
	/* SYSTEM namespace */
	if (!strcmp(name, "posix_acl_access")) {
		*index = EXT4_XATTR_INDEX_POSIX_ACL_ACCESS;
		*e4name = "";
	} else if (!strcmp(name, "posix_acl_default")) {
		*index = EXT4_XATTR_INDEX_POSIX_ACL_DEFAULT;
		*e4name = "";
	} else if (!strncmp(name, "trusted.", 8)) {
		*index = EXT4_XATTR_INDEX_TRUSTED;
		*e4name = name + 8;
	} else if (!strncmp(name, "security.", 9)) {
		*index = EXT4_XATTR_INDEX_SECURITY;
		*e4name = name + 9;
	} else {
		*index = EXT4_XATTR_INDEX_SYSTEM;
		*e4name = name;
	}
	return TRUE;
}

/*===========================================================================*
 *				map_from_ext4				     *
 *===========================================================================*/
static int map_from_ext4(u8_t index, const char *e4name, size_t e4len,
	int *ns, char *full, size_t fullsz)
{
/* Translate an on-disk (index, name) into the (BSD namespace, full name) the
 * VFS uses, into 'full' (NUL-terminated).  Returns FALSE for an index we do not
 * expose. */
	const char *prefix = NULL;

	switch (index) {
	case EXT4_XATTR_INDEX_USER:		*ns = EXTATTR_NAMESPACE_USER;
						break;
	case EXT4_XATTR_INDEX_POSIX_ACL_ACCESS:	*ns = EXTATTR_NAMESPACE_SYSTEM;
						e4name = "posix_acl_access";
						e4len = 16;
						break;
	case EXT4_XATTR_INDEX_POSIX_ACL_DEFAULT:*ns = EXTATTR_NAMESPACE_SYSTEM;
						e4name = "posix_acl_default";
						e4len = 17;
						break;
	case EXT4_XATTR_INDEX_TRUSTED:		*ns = EXTATTR_NAMESPACE_SYSTEM;
						prefix = "trusted.";
						break;
	case EXT4_XATTR_INDEX_SECURITY:		*ns = EXTATTR_NAMESPACE_SYSTEM;
						prefix = "security.";
						break;
	case EXT4_XATTR_INDEX_SYSTEM:		*ns = EXTATTR_NAMESPACE_SYSTEM;
						break;
	default:				return FALSE;
	}

	if (prefix != NULL) {
		size_t pl = strlen(prefix);
		if (pl + e4len + 1 > fullsz)
			return FALSE;
		memcpy(full, prefix, pl);
		memcpy(full + pl, e4name, e4len);
		full[pl + e4len] = '\0';
	} else {
		if (e4len + 1 > fullsz)
			return FALSE;
		memcpy(full, e4name, e4len);
		full[e4len] = '\0';
	}
	return TRUE;
}

/*===========================================================================*
 *				xattr_iter				     *
 *===========================================================================*/
/* Iterate the entries of one attribute region.  'entries' points at the first
 * entry, 'value_base' is where e_value_offs is measured from, and 'limit' is
 * the first byte past the region.  For each well-formed entry the callback is
 * invoked with the decoded fields; it returns nonzero to stop early. */
typedef int (*xattr_cb)(void *arg, u8_t index, const char *name, size_t namelen,
	const char *value, size_t valuelen);

static int xattr_iter(const char *entries, const char *value_base,
	const char *limit, xattr_cb cb, void *arg)
{
	const char *p = entries;

	while (p + XATTR_ENT_SIZE <= limit) {
		const struct ext2_xattr_entry *e =
		    (const struct ext2_xattr_entry *) p;
		size_t namelen = e->e_name_len;
		const char *name = p + XATTR_ENT_SIZE;
		u32_t voffs, vsize, vinum;

		if (e->e_name_len == 0 && e->e_name_index == 0)
			break;			/* end-of-entries terminator */
		if (name + namelen > limit)
			break;			/* corrupt: name overruns */

		voffs = e->e_value_offs;
		vsize = e->e_value_size;
		vinum = e->e_value_inum;

		/* Skip values held in a separate inode (ea_inode feature) and
		 * any value that does not lie within the region. */
		if (vinum == 0 && value_base + voffs + vsize <= limit &&
		    value_base + voffs >= entries) {
			if (cb(arg, e->e_name_index, name, namelen,
			    value_base + voffs, vsize))
				return TRUE;
		}
		p += XATTR_ENT_PAD(namelen);
	}
	return FALSE;
}

/*===========================================================================*
 *				inode_xattr_region			     *
 *===========================================================================*/
static struct buf *inode_xattr_region(struct inode *rip, const char **entries,
	const char **value_base, const char **limit)
{
/* If the inode has an in-inode attribute region, return the block buffer
 * holding the on-disk inode and set the region pointers; otherwise NULL.  The
 * caller must put_block() the returned buffer. */
	struct super_block *sp = rip->i_sp;
	struct group_desc *gd;
	struct buf *bp;
	const char *dip;
	block64_t b;
	u32_t offset;
	u16_t extra;
	unsigned int ihdr, isize = EXT2_INODE_SIZE(sp);

	if (isize <= EXT2_GOOD_OLD_INODE_SIZE)
		return NULL;			/* no room for in-inode attrs */

	gd = get_group_desc((rip->i_num - 1) / sp->s_inodes_per_group);
	if (gd == NULL)
		return NULL;
	offset = ((rip->i_num - 1) % sp->s_inodes_per_group) * isize;
	b = ext2_gd_inode_table(sp, gd) + (offset >> sp->s_blocksize_bits);
	if ((bp = get_block(rip->i_dev, b, NORMAL)) == NULL)
		return NULL;
	dip = b_data(bp) + (offset & (sp->s_block_size - 1));

	memcpy(&extra, dip + INO_EXTRA_ISIZE_OFF, sizeof(extra));
	ihdr = EXT2_GOOD_OLD_INODE_SIZE + extra;
	if (ihdr + sizeof(u32_t) > isize) {
		put_block(bp);
		return NULL;
	}
	if (*(const u32_t *)(dip + ihdr) != EXT2_XATTR_MAGIC) {
		put_block(bp);
		return NULL;			/* no attributes here */
	}
	*entries = dip + ihdr + sizeof(u32_t);
	*value_base = *entries;			/* in-inode: offset from IFIRST */
	*limit = dip + isize;
	return bp;
}

/*===========================================================================*
 *				block_xattr_region			     *
 *===========================================================================*/
static struct buf *block_xattr_region(struct inode *rip, const char **entries,
	const char **value_base, const char **limit)
{
/* If the inode has an external attribute block, return its buffer and set the
 * region pointers; otherwise NULL.  The caller must put_block() the buffer. */
	struct super_block *sp = rip->i_sp;
	struct buf *bp;
	const char *blk;
	const struct ext2_xattr_block_header *h;

	if (rip->i_file_acl == 0)
		return NULL;
	if ((bp = get_block(rip->i_dev, rip->i_file_acl, NORMAL)) == NULL)
		return NULL;
	blk = b_data(bp);
	h = (const struct ext2_xattr_block_header *) blk;
	if (h->h_magic != EXT2_XATTR_MAGIC) {
		put_block(bp);
		return NULL;
	}
	*entries = blk + XATTR_BLOCK_HDR_SIZE;
	*value_base = blk;			/* in-block: offset from block */
	*limit = blk + sp->s_block_size;
	return bp;
}

/* ext4 on-disk ACL format (ext4_acl_header / ext4_acl_entry). */
#define EXT4_ACL_VERSION	0x0001

static int is_acl_index(u8_t index)
{
	return index == EXT4_XATTR_INDEX_POSIX_ACL_ACCESS ||
	    index == EXT4_XATTR_INDEX_POSIX_ACL_DEFAULT;
}

/*===========================================================================*
 *				ext4_acl_to_posix			     *
 *===========================================================================*/
static int ext4_acl_to_posix(const char *in, size_t inlen, char *out,
	size_t outcap, size_t *outlen)
{
/* Convert an ext4 on-disk ACL (version 1; named entries carry a 4-byte id,
 * others do not) into the generic POSIX_ACL_XATTR form (version 2; every entry
 * is a fixed 8-byte {tag, perm, id}).  This is what acl_get_file()/getfacl and
 * the VFS permission check expect. */
	const u8_t *p = (const u8_t *) in;
	const u8_t *end = p + inlen;
	u8_t *o = (u8_t *) out;
	u32_t ver;

	if (inlen < 4)
		return EINVAL;
	memcpy(&ver, p, 4);
	if (ver != EXT4_ACL_VERSION)
		return EINVAL;
	p += 4;

	if (outcap < 4)
		return ERANGE;
	{ u32_t v2 = POSIX_ACL_XATTR_VERSION; memcpy(o, &v2, 4); }
	o += 4;

	while (p + 4 <= end) {
		u16_t tag, perm;
		u32_t id = (u32_t) ACL_UNDEFINED_ID;

		memcpy(&tag, p, 2);
		memcpy(&perm, p + 2, 2);
		p += 4;
		if (tag == ACL_USER || tag == ACL_GROUP) {
			if (p + 4 > end)
				return EINVAL;
			memcpy(&id, p, 4);
			p += 4;
		}
		if ((size_t)(o - (u8_t *) out) + 8 > outcap)
			return ERANGE;
		memcpy(o, &tag, 2);
		memcpy(o + 2, &perm, 2);
		memcpy(o + 4, &id, 4);
		o += 8;
	}
	*outlen = (size_t)(o - (u8_t *) out);
	return OK;
}

/* --- get one attribute --- */
struct find_ctx {
	u8_t want_index;
	const char *want_name;
	size_t want_namelen;
	const char *value;
	size_t valuelen;
	int found;
};

static int find_cb(void *arg, u8_t index, const char *name, size_t namelen,
	const char *value, size_t valuelen)
{
	struct find_ctx *c = arg;

	if (index == c->want_index && namelen == c->want_namelen &&
	    !memcmp(name, c->want_name, namelen)) {
		c->value = value;
		c->valuelen = valuelen;
		c->found = TRUE;
		return TRUE;
	}
	return FALSE;
}

/*===========================================================================*
 *				fs_getxattr				     *
 *===========================================================================*/
ssize_t fs_getxattr(ino_t ino_nr, int attrnamespace, const char *name,
	struct fsdriver_data *data, size_t bytes)
{
/* Return the value of one extended attribute (or, with bytes == 0, its size). */
	static char valbuf[65536];	/* stable copy of the (converted) value */
	struct inode *rip;
	struct buf *bp;
	const char *entries, *value_base, *limit, *e4name;
	struct find_ctx ctx;
	u8_t index;
	size_t namelen, vallen = 0;
	ssize_t r;
	int pass, found = 0;

	if (!xattr_ns_ok(attrnamespace))
		return(EINVAL);
	if ((rip = find_inode(fs_dev, ino_nr)) == NULL)
		return(EINVAL);
	namelen = strlen(name);
	if (namelen == 0 || namelen > XATTR_NAME_MAX)
		return(EINVAL);
	if (!map_to_ext4(attrnamespace, name, &index, &e4name))
		return(ENOATTR);

	/* Search the in-inode region first, then the external block.  Copy the
	 * value into valbuf while its block is held (converting an ACL from the
	 * ext4 on-disk form to the generic POSIX_ACL_XATTR form). */
	for (pass = 0; pass < 2 && !found; pass++) {
		bp = (pass == 0) ?
		    inode_xattr_region(rip, &entries, &value_base, &limit) :
		    block_xattr_region(rip, &entries, &value_base, &limit);
		if (bp == NULL)
			continue;
		memset(&ctx, 0, sizeof(ctx));
		ctx.want_index = index;
		ctx.want_name = e4name;
		ctx.want_namelen = strlen(e4name);
		xattr_iter(entries, value_base, limit, find_cb, &ctx);
		if (ctx.found) {
			if (is_acl_index(index)) {
				if (ext4_acl_to_posix(ctx.value, ctx.valuelen,
				    valbuf, sizeof(valbuf), &vallen) != OK) {
					put_block(bp);
					return(EINVAL);
				}
			} else if (ctx.valuelen <= sizeof(valbuf)) {
				memcpy(valbuf, ctx.value, ctx.valuelen);
				vallen = ctx.valuelen;
			} else {
				put_block(bp);
				return(ERANGE);
			}
			found = 1;
		}
		put_block(bp);
	}

	if (!found)
		return(ENOATTR);
	if (bytes == 0)
		return((ssize_t) vallen);
	if (bytes < vallen)
		return(ERANGE);
	if (vallen == 0)
		return(0);
	if ((r = fsdriver_copyout(data, 0, valbuf, vallen)) == OK)
		r = (ssize_t) vallen;
	return(r);
}

/* --- list attributes --- */
struct list_ctx {
	int ns;				/* requested namespace */
	char *buf;			/* accumulation buffer (LENPREFIX) */
	size_t cap;
	size_t len;
	int overflow;
};

static int list_cb(void *arg, u8_t index, const char *name, size_t namelen,
	const char *value, size_t valuelen)
{
	struct list_ctx *c = arg;
	int ns;
	char full[XATTR_NAME_MAX + 16];
	size_t fl;

	(void) value;
	(void) valuelen;
	if (!map_from_ext4(index, name, namelen, &ns, full, sizeof(full)))
		return FALSE;
	if (ns != c->ns)
		return FALSE;
	fl = strlen(full);
	if (fl == 0 || fl > 0xFF)
		return FALSE;
	/* EXTATTR_LIST_LENPREFIX: one length byte then the name (no NUL). */
	if (c->len + 1 + fl > c->cap) {
		c->overflow = TRUE;
		return FALSE;
	}
	c->buf[c->len++] = (char)(unsigned char) fl;
	memcpy(c->buf + c->len, full, fl);
	c->len += fl;
	return FALSE;
}

/*===========================================================================*
 *				fs_listxattr				     *
 *===========================================================================*/
ssize_t fs_listxattr(ino_t ino_nr, int attrnamespace,
	struct fsdriver_data *data, size_t bytes)
{
/* List the names of the attributes in one namespace (or, with bytes == 0, the
 * size such a list would need). */
	static char listbuf[2 * (XATTR_NAME_MAX + 16)];
	struct inode *rip;
	struct buf *bp;
	const char *entries, *value_base, *limit;
	struct list_ctx ctx;
	ssize_t r;
	int pass;

	if (!xattr_ns_ok(attrnamespace))
		return(EINVAL);
	if ((rip = find_inode(fs_dev, ino_nr)) == NULL)
		return(EINVAL);

	memset(&ctx, 0, sizeof(ctx));
	ctx.ns = attrnamespace;
	ctx.buf = listbuf;
	ctx.cap = sizeof(listbuf);

	for (pass = 0; pass < 2; pass++) {
		bp = (pass == 0) ?
		    inode_xattr_region(rip, &entries, &value_base, &limit) :
		    block_xattr_region(rip, &entries, &value_base, &limit);
		if (bp == NULL)
			continue;
		xattr_iter(entries, value_base, limit, list_cb, &ctx);
		put_block(bp);
	}
	if (ctx.overflow)
		return(ERANGE);

	if (bytes == 0)
		return((ssize_t) ctx.len);
	if (bytes < ctx.len)
		return(ERANGE);
	if (ctx.len == 0)
		return(0);
	if ((r = fsdriver_copyout(data, 0, listbuf, ctx.len)) != OK)
		return(r);
	return((ssize_t) ctx.len);
}
