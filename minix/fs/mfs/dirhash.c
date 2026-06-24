/* In-memory directory hashing for MFS.
 *
 * MFS looks names up in a directory with a linear scan of every entry
 * (search_dir() in path.c), which is O(n) and slow for large directories.
 * This module keeps an optional in-memory hash table, attached to a
 * directory's in-core inode, mapping each entry name to its byte position in
 * the directory file.  It is a pure accelerator: it holds no on-disk state and
 * needs no format change, so it benefits V1/V2/V3 and V4 filesystems alike.
 *
 * The hash is built lazily on the first lookup of a sufficiently large
 * directory, maintained incrementally as entries are added and removed, and
 * freed when the inode is evicted, truncated, or reused.  A hit is verified
 * against the on-disk entry before use; the total memory across all directory
 * hashes is bounded by DIRHASH_MEM_MAX, and a directory whose hash cannot be
 * maintained within that budget simply falls back to the linear scan.
 */

#include "fs.h"
#include "buf.h"
#include "inode.h"
#include "super.h"
#include <stdlib.h>
#include <string.h>

/* Only hash directories of at least this many bytes (a few blocks), and keep
 * the combined size of all directory hashes under the memory cap. */
#define DIRHASH_MIN_SIZE	(8 * 1024)
#define DIRHASH_MEM_MAX		(4 * 1024 * 1024)

struct dirhash_ent {
  off_t dh_pos;				/* byte offset of the entry in the dir */
  struct dirhash_ent *dh_next;		/* hash-collision chain */
  char dh_name[MFS_DIRSIZ];		/* entry name, NUL-padded */
};

struct dirhash {
  unsigned int dh_nbuckets;
  unsigned int dh_nentries;
  size_t dh_mem;			/* bytes charged to this hash */
  struct dirhash_ent **dh_tab;
};

static size_t dirhash_total;		/* bytes used by all directory hashes */

/*===========================================================================*
 *				name_hash				     *
 *===========================================================================*/
static unsigned int name_hash(const char *name)
{
/* FNV-1a hash over the entry name (up to a NUL or MFS_DIRSIZ bytes). */
  unsigned int h = 2166136261u;
  int i;

  for (i = 0; i < MFS_DIRSIZ && name[i] != '\0'; i++) {
	h ^= (unsigned char) name[i];
	h *= 16777619u;
  }
  return h;
}

/*===========================================================================*
 *				dirhash_free				     *
 *===========================================================================*/
void dirhash_free(struct inode *rip)
{
/* Discard a directory's in-memory hash, if any. */
  struct dirhash *dh;
  struct dirhash_ent *ep, *next;
  unsigned int i;

  if ((dh = rip->i_dirhash) == NULL) return;

  for (i = 0; i < dh->dh_nbuckets; i++) {
	for (ep = dh->dh_tab[i]; ep != NULL; ep = next) {
		next = ep->dh_next;
		free(ep);
	}
  }
  free(dh->dh_tab);
  dirhash_total -= dh->dh_mem;
  free(dh);
  rip->i_dirhash = NULL;
}

/*===========================================================================*
 *				dh_insert				     *
 *===========================================================================*/
static int dh_insert(struct dirhash *dh, const char *name, off_t pos)
{
/* Add a name->position mapping.  Returns OK, or ENOSPC if the memory budget
 * is exhausted (the caller then drops the whole hash). */
  struct dirhash_ent *ep;
  unsigned int b;

  if (dirhash_total + sizeof(*ep) > DIRHASH_MEM_MAX)
	return(ENOSPC);
  if ((ep = malloc(sizeof(*ep))) == NULL)
	return(ENOSPC);

  memcpy(ep->dh_name, name, MFS_DIRSIZ);
  ep->dh_pos = pos;
  b = name_hash(name) & (dh->dh_nbuckets - 1);
  ep->dh_next = dh->dh_tab[b];
  dh->dh_tab[b] = ep;
  dh->dh_nentries++;
  dh->dh_mem += sizeof(*ep);
  dirhash_total += sizeof(*ep);
  return(OK);
}

/*===========================================================================*
 *				dirhash_build				     *
 *===========================================================================*/
void dirhash_build(struct inode *rip)
{
/* Build the in-memory hash for directory 'rip' by scanning it once, unless it
 * is too small to be worth it or the memory budget does not allow it. */
  struct dirhash *dh;
  struct buf *bp;
  struct direct *dp;
  unsigned int nb, blk_ents, i;
  size_t tabmem;
  off_t pos, epos;
  unsigned int block_size;

  if (rip->i_dirhash != NULL) return;
  if ((rip->i_mode & I_TYPE) != I_DIRECTORY) return;
  if (rip->i_size < DIRHASH_MIN_SIZE) return;

  /* Pick a power-of-two bucket count of roughly the entry count. */
  nb = 64;
  while (nb < (unsigned int)(rip->i_size / DIR_ENTRY_SIZE) && nb < (1u << 20))
	nb <<= 1;
  tabmem = nb * sizeof(struct dirhash_ent *);
  if (dirhash_total + tabmem > DIRHASH_MEM_MAX) return;

  if ((dh = malloc(sizeof(*dh))) == NULL) return;
  if ((dh->dh_tab = malloc(tabmem)) == NULL) {
	free(dh);
	return;
  }
  memset(dh->dh_tab, 0, tabmem);
  dh->dh_nbuckets = nb;
  dh->dh_nentries = 0;
  dh->dh_mem = tabmem;
  dirhash_total += tabmem;
  rip->i_dirhash = dh;

  block_size = rip->i_sp->s_block_size;
  blk_ents = NR_DIR_ENTRIES(block_size);

  for (pos = 0; pos < rip->i_size; pos += block_size) {
	bp = get_block_map(rip, pos);
	if (bp == NULL) continue;
	for (i = 0; i < blk_ents; i++) {
		epos = pos + (off_t) i * DIR_ENTRY_SIZE;
		if (epos >= rip->i_size) break;
		dp = &b_dir(bp)[i];
		if (dp->mfs_d_ino == NO_ENTRY) continue;
		if (dh_insert(dh, dp->mfs_d_name, epos) != OK) {
			/* Out of budget: a partial hash is unsafe, drop it. */
			put_block(bp);
			dirhash_free(rip);
			return;
		}
	}
	put_block(bp);
  }
}

/*===========================================================================*
 *				dirhash_lookup				     *
 *===========================================================================*/
int dirhash_lookup(struct inode *rip, const char *name, off_t *pos)
{
/* If 'name' is in the directory's hash, store its position in *pos and return
 * TRUE.  A FALSE result means the name is not present (the hash is complete). */
  struct dirhash *dh;
  struct dirhash_ent *ep;
  unsigned int b;

  if ((dh = rip->i_dirhash) == NULL) return(FALSE);

  b = name_hash(name) & (dh->dh_nbuckets - 1);
  for (ep = dh->dh_tab[b]; ep != NULL; ep = ep->dh_next) {
	if (strncmp(ep->dh_name, name, MFS_DIRSIZ) == 0) {
		*pos = ep->dh_pos;
		return(TRUE);
	}
  }
  return(FALSE);
}

/*===========================================================================*
 *				dirhash_enter				     *
 *===========================================================================*/
void dirhash_enter(struct inode *rip, const char *name, off_t pos)
{
/* Record that 'name' was just added at byte position 'pos'.  If the hash
 * cannot absorb the new entry, it is dropped so it never goes stale. */
  if (rip->i_dirhash == NULL) return;

  if (dh_insert(rip->i_dirhash, name, pos) != OK)
	dirhash_free(rip);
}

/*===========================================================================*
 *				dirhash_remove				     *
 *===========================================================================*/
void dirhash_remove(struct inode *rip, const char *name)
{
/* Remove 'name' from the directory's hash, if present. */
  struct dirhash *dh;
  struct dirhash_ent *ep, **pp;
  unsigned int b;

  if ((dh = rip->i_dirhash) == NULL) return;

  b = name_hash(name) & (dh->dh_nbuckets - 1);
  for (pp = &dh->dh_tab[b]; (ep = *pp) != NULL; pp = &ep->dh_next) {
	if (strncmp(ep->dh_name, name, MFS_DIRSIZ) == 0) {
		*pp = ep->dh_next;
		free(ep);
		dh->dh_nentries--;
		dh->dh_mem -= sizeof(*ep);
		dirhash_total -= sizeof(*ep);
		return;
	}
  }
}
