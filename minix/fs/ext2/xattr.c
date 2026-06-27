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
 * Both read (get/list) and write (set/remove) are implemented.  On a write the
 * inode's whole attribute set is rebuilt into the external block (any in-inode
 * region is consolidated and emptied); a shared block is copied-on-write, and an
 * empty set frees the block.  Per-entry e_hash and, on metadata_csum volumes,
 * the block checksum are maintained so e2fsck stays clean.
 *
 * Created:
 *   June 2026 (ext4 xattr support)
 */

#include "fs.h"
#include <string.h>
#include <sys/extattr.h>
#include <sys/acl.h>
#include <sys/xattr.h>
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

#define E2_XATTR_NAME_MAX	255

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
#define XATTR_ENT_SIZE		16	/* fixed part (incl e_hash), before name */
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
	if (namelen == 0 || namelen > E2_XATTR_NAME_MAX)
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
	char full[E2_XATTR_NAME_MAX + 16];
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
	static char listbuf[2 * (E2_XATTR_NAME_MAX + 16)];
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

/* ========================== write side ========================== */

#define NAME_HASH_SHIFT		5
#define VALUE_HASH_SHIFT	16

/*===========================================================================*
 *				xattr_hash_entry			     *
 *===========================================================================*/
static u32_t xattr_hash_entry(const char *name, size_t namelen,
	const char *value, size_t vallen)
{
/* Per-entry hash (e_hash): over the name bytes, then the value as 32-bit
 * little-endian words (zero-padded to a word boundary).  Matches e2fsprogs. */
	u32_t h = 0;
	size_t i;

	for (i = 0; i < namelen; i++)
		h = (h << NAME_HASH_SHIFT) ^ (h >> (32 - NAME_HASH_SHIFT)) ^
		    (u8_t) name[i];
	if (vallen > 0) {
		size_t words = (vallen + 3) / 4, j;
		const u8_t *v = (const u8_t *) value;
		for (i = 0; i < words; i++) {
			u32_t w = 0;
			for (j = 0; j < 4; j++)
				if (i * 4 + j < vallen)
					w |= (u32_t) v[i * 4 + j] << (8 * j);
			h = (h << VALUE_HASH_SHIFT) ^
			    (h >> (32 - VALUE_HASH_SHIFT)) ^ w;
		}
	}
	return h;
}

/*===========================================================================*
 *				xattr_rehash_block			     *
 *===========================================================================*/
static void xattr_rehash_block(char *blk, unsigned int bs)
{
/* Recompute every entry's e_hash.  h_hash is left 0, as mke2fs does and e2fsck
 * accepts (it is only an in-kernel sharing hint). */
	char *p = blk + XATTR_BLOCK_HDR_SIZE;

	while (p + XATTR_ENT_SIZE <= blk + bs) {
		struct ext2_xattr_entry *e = (struct ext2_xattr_entry *) p;
		size_t nl = e->e_name_len;

		if (e->e_name_len == 0 && e->e_name_index == 0)
			break;
		e->e_hash = xattr_hash_entry(p + XATTR_ENT_SIZE, nl,
		    blk + e->e_value_offs, e->e_value_size);
		p += XATTR_ENT_PAD(nl);
	}
}

/*===========================================================================*
 *				ext4_acl_to_disk			     *
 *===========================================================================*/
static int ext4_acl_to_disk(const char *in, size_t inlen, char *out,
	size_t outcap, size_t *outlen)
{
/* Reverse of ext4_acl_to_posix: a generic POSIX_ACL_XATTR value (version 2,
 * fixed 8-byte entries) into the ext4 on-disk ACL (version 1, named entries
 * carry a 4-byte id, others do not).  Entries are sorted into the canonical
 * POSIX order -- USER_OBJ, USER (by id), GROUP_OBJ, GROUP (by id), MASK, OTHER,
 * which is exactly ascending (tag, id) as the tags are ordered powers of two --
 * because ext4 and e2fsck require it (MINIX's setfacl does not sort). */
	struct aent { u16_t tag, perm; u32_t id; } ent[64];
	const u8_t *p = (const u8_t *) in, *end = p + inlen;
	u8_t *o = (u8_t *) out;
	u32_t ver;
	int n = 0, i, j;

	if (inlen < 4 || (inlen - 4) % 8 != 0)
		return EINVAL;
	memcpy(&ver, p, 4);
	if (ver != POSIX_ACL_XATTR_VERSION)
		return EINVAL;
	p += 4;
	while (p + 8 <= end && n < (int)(sizeof(ent) / sizeof(ent[0]))) {
		memcpy(&ent[n].tag, p, 2);
		memcpy(&ent[n].perm, p + 2, 2);
		memcpy(&ent[n].id, p + 4, 4);
		p += 8;
		n++;
	}
	if (p != end)				/* more entries than we can hold */
		return E2BIG;

	for (i = 0; i < n; i++)
		for (j = i + 1; j < n; j++)
			if (ent[j].tag < ent[i].tag ||
			    (ent[j].tag == ent[i].tag &&
			     ent[j].id < ent[i].id)) {
				struct aent t = ent[i];
				ent[i] = ent[j];
				ent[j] = t;
			}

	if (outcap < 4)
		return ERANGE;
	{ u32_t v = EXT4_ACL_VERSION; memcpy(o, &v, 4); }
	o += 4;
	for (i = 0; i < n; i++) {
		if ((size_t)(o - (u8_t *) out) + 4 > outcap)
			return ERANGE;
		memcpy(o, &ent[i].tag, 2);
		memcpy(o + 2, &ent[i].perm, 2);
		o += 4;
		if (ent[i].tag == ACL_USER || ent[i].tag == ACL_GROUP) {
			if ((size_t)(o - (u8_t *) out) + 4 > outcap)
				return ERANGE;
			memcpy(o, &ent[i].id, 4);
			o += 4;
		}
	}
	*outlen = (size_t)(o - (u8_t *) out);
	return OK;
}

/*===========================================================================*
 *				append_attr				     *
 *===========================================================================*/
static int append_attr(char *blk, size_t *eoff, size_t *voff, unsigned int bs,
	u8_t index, const char *name, size_t namelen, const char *value,
	size_t vallen)
{
/* Append one attribute to a block being built: its entry grows up from *eoff,
 * its value grows down from *voff.  Returns 0 if it would not fit (4 bytes are
 * reserved for the end-of-entries terminator). */
	size_t entsz = XATTR_ENT_PAD(namelen);
	size_t valsz = (vallen + 3) & ~(size_t)3;
	struct ext2_xattr_entry *e;

	if (*eoff + entsz + 4 > *voff - valsz)
		return 0;
	*voff -= valsz;
	if (vallen > 0)
		memcpy(blk + *voff, value, vallen);
	e = (struct ext2_xattr_entry *)(blk + *eoff);
	e->e_name_len = (u8_t) namelen;
	e->e_name_index = index;
	e->e_value_offs = (u16_t) *voff;	/* in-block: offset from start */
	e->e_value_inum = 0;
	e->e_value_size = (u32_t) vallen;
	e->e_hash = 0;				/* filled by xattr_rehash_block */
	memcpy(blk + *eoff + XATTR_ENT_SIZE, name, namelen);
	*eoff += entsz;
	return 1;
}

/*===========================================================================*
 *				write_xattr_block			     *
 *===========================================================================*/
static int write_xattr_block(struct inode *rip, const char *newblk,
	unsigned int bs)
{
/* Store the rebuilt attribute block.  If the inode's current block is shared,
 * copy-on-write to a fresh block (dropping the shared block's count); otherwise
 * reuse it, or allocate one if there is none. */
	struct super_block *sp = rip->i_sp;
	struct buf *bp;
	block64_t target = rip->i_file_acl;
	int had_block = (target != 0);

	if (target != 0) {
		struct ext2_xattr_block_header *h;
		u32_t refc;

		if ((bp = get_block(rip->i_dev, target, NORMAL)) == NULL)
			return EIO;
		h = (struct ext2_xattr_block_header *) b_data(bp);
		refc = h->h_refcount;
		if (h->h_magic == EXT2_XATTR_MAGIC && refc > 1) {
			h->h_refcount = refc - 1;
			ext2_xattr_block_csum_set(sp, target, b_data(bp));
			lmfs_markdirty(bp);
			put_block(bp);
			target = 0;		/* must not modify a shared block */
		} else
			put_block(bp);		/* reuse it */
	}
	if (target == 0) {
		if ((target = alloc_block(rip, rip->i_bsearch)) == NO_BLOCK)
			return ENOSPC;
		rip->i_file_acl = target;
		if (!had_block)
			rip->i_blocks += sp->s_sectors_in_block;
	}

	if ((bp = get_block(rip->i_dev, target, NO_READ)) == NULL)
		return EIO;
	memcpy(b_data(bp), newblk, bs);
	ext2_xattr_block_csum_set(sp, target, b_data(bp));
	lmfs_markdirty(bp);
	put_block(bp);
	rip->i_dirt = IN_DIRTY;
	return OK;
}

/*===========================================================================*
 *				clear_inode_xattrs			     *
 *===========================================================================*/
static void clear_inode_xattrs(struct inode *rip)
{
/* Drop the in-inode attribute region (its contents have been moved into the
 * block) by zeroing its magic, and refresh the inode checksum that covers it. */
	struct super_block *sp = rip->i_sp;
	struct group_desc *gd;
	struct buf *bp;
	char *dip;
	block64_t b;
	u32_t offset, zero = 0, magic;
	u16_t extra;
	unsigned int ihdr, isize = EXT2_INODE_SIZE(sp);

	if (isize <= EXT2_GOOD_OLD_INODE_SIZE)
		return;
	gd = get_group_desc((rip->i_num - 1) / sp->s_inodes_per_group);
	if (gd == NULL)
		return;
	offset = ((rip->i_num - 1) % sp->s_inodes_per_group) * isize;
	b = ext2_gd_inode_table(sp, gd) + (offset >> sp->s_blocksize_bits);
	if ((bp = get_block(rip->i_dev, b, NORMAL)) == NULL)
		return;
	dip = b_data(bp) + (offset & (sp->s_block_size - 1));
	memcpy(&extra, dip + INO_EXTRA_ISIZE_OFF, sizeof(extra));
	ihdr = EXT2_GOOD_OLD_INODE_SIZE + extra;
	if (ihdr + sizeof(u32_t) <= isize) {
		memcpy(&magic, dip + ihdr, sizeof(magic));
		if (magic == EXT2_XATTR_MAGIC) {
			memcpy(dip + ihdr, &zero, sizeof(zero));
			ext2_inode_csum_set(sp, rip->i_num, dip);
			lmfs_markdirty(bp);
		}
	}
	put_block(bp);
}

/*===========================================================================*
 *				xattr_apply				     *
 *===========================================================================*/
static int xattr_apply(struct inode *rip, u8_t index, const char *name,
	size_t namelen, const char *value, size_t vallen, int is_remove,
	int flags)
{
/* Rebuild the inode's attribute set with one attribute added/replaced/removed,
 * consolidating everything into the external block. */
	struct super_block *sp = rip->i_sp;
	unsigned int bs = sp->s_block_size;
	static char newblk[65536];
	struct buf *bp;
	const char *entries, *value_base, *limit;
	size_t eoff = XATTR_BLOCK_HDR_SIZE, voff = bs;
	int found = 0, nospc = 0, had_inode = 0, pass;
	struct ext2_xattr_block_header *nh;

	memset(newblk, 0, bs);
	nh = (struct ext2_xattr_block_header *) newblk;
	nh->h_magic = EXT2_XATTR_MAGIC;
	nh->h_refcount = 1;
	nh->h_blocks = 1;

	for (pass = 0; pass < 2; pass++) {
		const char *p;

		bp = (pass == 0) ?
		    inode_xattr_region(rip, &entries, &value_base, &limit) :
		    block_xattr_region(rip, &entries, &value_base, &limit);
		if (bp == NULL)
			continue;
		if (pass == 0)
			had_inode = 1;
		p = entries;
		while (p + XATTR_ENT_SIZE <= limit) {
			const struct ext2_xattr_entry *e =
			    (const struct ext2_xattr_entry *) p;
			size_t nl = e->e_name_len;
			const char *nm = p + XATTR_ENT_SIZE;
			u32_t vo, vs, vi;

			if (e->e_name_len == 0 && e->e_name_index == 0)
				break;
			if (nm + nl > limit)
				break;
			vo = e->e_value_offs;
			vs = e->e_value_size;
			vi = e->e_value_inum;
			if (vi == 0 && value_base + vo + vs <= limit &&
			    value_base + vo >= entries) {
				if (e->e_name_index == index &&
				    nl == namelen &&
				    !memcmp(nm, name, namelen)) {
					found = 1;	/* skip: it is the target */
				} else if (!append_attr(newblk, &eoff, &voff,
				    bs, e->e_name_index, nm, nl,
				    value_base + vo, vs)) {
					nospc = 1;
				}
			}
			p += XATTR_ENT_PAD(nl);
		}
		put_block(bp);
		if (nospc)
			return(ENOSPC);
	}

	if (is_remove) {
		if (!found)
			return(ENOATTR);
	} else {
		if ((flags & XATTR_CREATE) && found)
			return(EEXIST);
		if ((flags & XATTR_REPLACE) && !found)
			return(ENOATTR);
		if (!append_attr(newblk, &eoff, &voff, bs, index, name,
		    namelen, value, vallen))
			return(ENOSPC);
	}

	xattr_rehash_block(newblk, bs);

	if (eoff == XATTR_BLOCK_HDR_SIZE)
		ext2_free_xattr_block(rip);	/* nothing left: free the block */
	else {
		int r = write_xattr_block(rip, newblk, bs);
		if (r != OK)
			return(r);
	}
	if (had_inode)
		clear_inode_xattrs(rip);

	rip->i_update |= CTIME;
	rip->i_dirt = IN_DIRTY;
	return(OK);
}

/*===========================================================================*
 *				fs_setxattr				     *
 *===========================================================================*/
int fs_setxattr(ino_t ino_nr, int attrnamespace, const char *name,
	struct fsdriver_data *data, size_t bytes, int flags)
{
/* Create or replace one extended attribute. */
	static char inbuf[65536], cvtbuf[65536];
	struct inode *rip;
	const char *e4name, *value;
	u8_t index;
	size_t vallen;
	int r;

	if (!xattr_ns_ok(attrnamespace))
		return(EINVAL);
	if ((rip = find_inode(fs_dev, ino_nr)) == NULL)
		return(EINVAL);
	if (rip->i_sp->s_rd_only)
		return(EROFS);
	if (strlen(name) == 0 || strlen(name) > E2_XATTR_NAME_MAX)
		return(EINVAL);
	if (bytes > sizeof(inbuf))
		return(E2BIG);
	if (!map_to_ext4(attrnamespace, name, &index, &e4name))
		return(EINVAL);

	if (bytes > 0 && (r = fsdriver_copyin(data, 0, inbuf, bytes)) != OK)
		return(r);
	value = inbuf;
	vallen = bytes;
	if (is_acl_index(index)) {
		if (ext4_acl_to_disk(inbuf, bytes, cvtbuf, sizeof(cvtbuf),
		    &vallen) != OK)
			return(EINVAL);
		value = cvtbuf;
	}

	r = xattr_apply(rip, index, e4name, strlen(e4name), value, vallen, 0,
	    flags);
	if (r == OK) {
		/* A volume that did not yet record any extended attribute may
		 * lack the (compatible) ext_attr feature; set it lazily the
		 * first time one is stored, as Linux does, so fsck accepts the
		 * i_file_acl reference. */
		if (!HAS_COMPAT_FEATURE(rip->i_sp, COMPAT_EXT_ATTR)) {
			rip->i_sp->s_feature_compat |= COMPAT_EXT_ATTR;
			write_super(rip->i_sp);
		}
		rw_inode(rip, WRITING);
	}
	return(r);
}

/*===========================================================================*
 *				fs_removexattr				     *
 *===========================================================================*/
int fs_removexattr(ino_t ino_nr, int attrnamespace, const char *name)
{
/* Remove one extended attribute. */
	struct inode *rip;
	const char *e4name;
	u8_t index;
	int r;

	if (!xattr_ns_ok(attrnamespace))
		return(EINVAL);
	if ((rip = find_inode(fs_dev, ino_nr)) == NULL)
		return(EINVAL);
	if (rip->i_sp->s_rd_only)
		return(EROFS);
	if (strlen(name) == 0 || strlen(name) > E2_XATTR_NAME_MAX)
		return(EINVAL);
	if (!map_to_ext4(attrnamespace, name, &index, &e4name))
		return(ENOATTR);

	r = xattr_apply(rip, index, e4name, strlen(e4name), NULL, 0, 1, 0);
	if (r == OK)
		rw_inode(rip, WRITING);
	return(r);
}
