/* tmpfs - a file system in memory
 *
 * Files, directories, symbolic links and device nodes live in this server's
 * memory only, and are gone when it is unmounted.  File data lives in VM's
 * cache: each page is handed to it as soon as it exists, under the file's
 * (dev, ino, offset) and a device offset unique to the page, and pinned there
 * (VMSF_PINNED), as the only copy, until tmpfs forgets it.  So mmap(2) of a
 * file maps these very pages, MAP_SHARED mappings write to them (they are
 * always "dirty", there being nothing to write them out to), and read(2) and
 * write(2), which go through a small window of pages that tmpfs maps for
 * itself, see what the mappings do and the other way around.
 *
 * Options (mount -o): size=N[k|m|g] limits the data held, by default half of
 * physical memory; nr_inodes=N limits the number of nodes; mode, uid and gid
 * set the root directory's.
 */

#include <minix/drivers.h>
#include <minix/fsdriver.h>
#include <minix/vfsif.h>
#include <minix/optset.h>
#include <minix/vm.h>
#include <machine/vmparam.h>
#include <sys/mman.h>
#include <sys/dirent.h>
#include <sys/statvfs.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define ROOT_INO	1

/* A page of file data, which lives in VM's cache. */
struct page {
	u32_t p_id;			/* for VM: device offset / PAGE_SIZE;
					 * 0 for a hole */
	u32_t p_slot;			/* 1 + its slot in the window, or 0 */
};

/*
 * The pages that we have mapped, to read and write them ourselves.  The rest
 * are in VM's cache only, pinned there (VMSF_PINNED) until we forget them, so
 * that what tmpfs holds is not limited by its own address space.
 */
#define WINDOW_PAGES	1024
static struct slot {
	struct page *s_page;		/* NULL: free */
	char *s_addr;
} window[WINDOW_PAGES];
static unsigned int window_hand;

/* A directory entry; a NULL name marks a free slot. */
struct dent {
	char *d_name;
	ino_t d_ino;
};

struct node {
	ino_t n_ino;
	mode_t n_mode;
	uid_t n_uid;
	gid_t n_gid;
	nlink_t n_nlink;		/* links from directories */
	unsigned int n_refs;		/* references held by VFS */
	int n_mountpt;			/* a file system is mounted on it */
	dev_t n_rdev;			/* for device nodes */
	off_t n_size;
	struct timespec n_atime, n_mtime, n_ctime, n_btime;

	/* Directories. */
	ino_t n_parent;
	struct dent *n_dents;
	size_t n_ndents;		/* slots */
	size_t n_used;			/* used slots */

	/* Regular files. */
	struct page **n_leaves;		/* see LEAF_PAGES */
	size_t n_nleaves;		/* slots in n_leaves */

	/* Symbolic links. */
	char *n_link;
};

static struct node **nodes;		/* by inode number; NULL: free */
static size_t nr_nodes;			/* slots in 'nodes' */
static size_t nodes_used;
static ino_t next_ino = ROOT_INO;	/* where to look for a free one */

static u32_t *free_ids;			/* page IDs given back */
static size_t nr_free_ids, max_free_ids;
static u32_t next_id = 1;		/* first never used */

static size_t pages_used;
static size_t max_pages;		/* size= */
static size_t max_nodes;		/* nr_inodes= */

static dev_t fs_dev;

/* Mount options. */
static char opt_size[32];
static int opt_nr_inodes;
static int opt_mode = 01777;
static int opt_uid, opt_gid;

static struct optset optset_table[] = {
	{ "size",	OPT_STRING,	opt_size,	sizeof(opt_size) },
	{ "nr_inodes",	OPT_INT,	&opt_nr_inodes,	10		},
	{ "mode",	OPT_INT,	&opt_mode,	8		},
	{ "uid",	OPT_INT,	&opt_uid,	10		},
	{ "gid",	OPT_INT,	&opt_gid,	10		},
	{ NULL,		0,		NULL,		0		}
};

static void
now(struct timespec *ts)
{

	(void)clock_time(ts);
}

static struct node *
get_node(ino_t ino)
{

	if (ino < ROOT_INO || ino >= nr_nodes)
		return NULL;
	return nodes[ino];
}

/*
 * Pages.
 */
static int
page_id_get(u32_t *id)
{

	if (nr_free_ids > 0) {
		*id = free_ids[--nr_free_ids];
		return OK;
	}
	if (next_id == UINT32_MAX)
		return ENOSPC;
	*id = next_id++;
	return OK;
}

static void
page_id_put(u32_t id)
{
	u32_t *p;
	size_t n;

	if (nr_free_ids == max_free_ids) {
		n = max_free_ids ? max_free_ids * 2 : 256;
		if ((p = realloc(free_ids, n * sizeof(*p))) == NULL)
			return;		/* leak the ID, not the memory */
		free_ids = p;
		max_free_ids = n;
	}
	free_ids[nr_free_ids++] = id;
}

static off_t
page_devoff(const struct page *pg)
{

	return (off_t)pg->p_id * PAGE_SIZE;
}

/*
 * A regular file's pages are kept in leaves, each a page of struct page, so
 * that no allocation grows with the file but the small array of leaves: one
 * pointer per LEAF_PAGES pages.  A struct page with no ID is a hole.
 */
#define LEAF_PAGES	(PAGE_SIZE / sizeof(struct page))

/* Take a slot in the window for 'pg', mapped at 'addr'. */
static void
window_put(struct page *pg, char *addr)
{
	struct slot *sl;

	sl = &window[window_hand];
	window_hand = (window_hand + 1) % WINDOW_PAGES;
	if (sl->s_page != NULL) {
		munmap(sl->s_addr, PAGE_SIZE);	/* VM keeps the page */
		sl->s_page->p_slot = 0;
	}
	sl->s_page = pg;
	sl->s_addr = addr;
	pg->p_slot = sl - window + 1;
}

/* Where we have page 'pg' of 'np', at offset 'pos', mapped. */
static char *
page_addr(struct node *np, off_t pos, struct page *pg)
{
	u32_t flags = 0;
	char *addr;

	if (pg->p_slot != 0)
		return window[pg->p_slot - 1].s_addr;
	addr = vm_map_cacheblock(fs_dev, page_devoff(pg), np->n_ino,
	    rounddown(pos, PAGE_SIZE), &flags, PAGE_SIZE);
	if (addr == MAP_FAILED)
		panic("tmpfs: VM lost a pinned page");
	window_put(pg, addr);
	return addr;
}

/*
 * Fill in 'pg' with a zero-filled page for 'np' at offset 'pos', in VM's cache
 * from now on and writable through shared mappings.
 */
static int
page_alloc(struct node *np, off_t pos, struct page *pg)
{
	u32_t flags = 0;
	char *addr;
	u32_t id;
	int r;

	if (pages_used >= max_pages)
		return ENOSPC;
	if ((r = page_id_get(&id)) != OK)
		return r;
	/* The pages are users' files: MAP_USERMEM keeps them out of the OOM
	 * reserve that system services have for themselves. */
	addr = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
	    MAP_PREALLOC | MAP_ANON | MAP_USERMEM, -1, 0);
	if (addr == MAP_FAILED) {
		page_id_put(id);
		return ENOSPC;
	}
	pg->p_id = id;
	if ((r = vm_set_cacheblock(addr, fs_dev, page_devoff(pg), np->n_ino,
	    pos, &flags, PAGE_SIZE, VMSF_PINNED)) != OK ||
	    (r = vm_mapwrite_cacheblock(fs_dev, page_devoff(pg), PAGE_SIZE,
	    1)) != OK) {
		printf("tmpfs: cannot give a page to VM (%d)\n", r);
		(void)vm_forget_cacheblock(fs_dev, page_devoff(pg), PAGE_SIZE);
		munmap(addr, PAGE_SIZE);
		page_id_put(id);
		pg->p_id = 0;
		return ENOSPC;
	}
	window_put(pg, addr);
	pages_used++;
	return OK;
}

/* Take a page from VM's cache, and from any shared mappings, and free it. */
static void
page_free(struct page *pg)
{
	struct slot *sl;

	if (pg->p_slot != 0) {
		sl = &window[pg->p_slot - 1];
		munmap(sl->s_addr, PAGE_SIZE);
		sl->s_page = NULL;
		pg->p_slot = 0;
	}
	(void)vm_forget_cacheblock(fs_dev, page_devoff(pg), PAGE_SIZE);
	page_id_put(pg->p_id);
	pg->p_id = 0;
	pages_used--;
}

/* The page of 'np' that holds offset 'pos', or NULL for a hole. */
static struct page *
page_find(struct node *np, off_t pos)
{
	size_t idx = (size_t)(pos / PAGE_SIZE);
	struct page *leaf;

	if (idx / LEAF_PAGES >= np->n_nleaves ||
	    (leaf = np->n_leaves[idx / LEAF_PAGES]) == NULL)
		return NULL;
	return leaf[idx % LEAF_PAGES].p_id != 0 ?
	    &leaf[idx % LEAF_PAGES] : NULL;
}

/* The page of 'np' that holds offset 'pos', allocated if need be. */
static struct page *
page_get(struct node *np, off_t pos, int *err)
{
	size_t idx = (size_t)(pos / PAGE_SIZE), l = idx / LEAF_PAGES, n;
	struct page **leaves, *pg;

	if (l >= np->n_nleaves) {
		for (n = np->n_nleaves ? np->n_nleaves : 4; n <= l; n *= 2)
			;
		if ((leaves = realloc(np->n_leaves,
		    n * sizeof(*leaves))) == NULL) {
			*err = ENOSPC;
			return NULL;
		}
		memset(&leaves[np->n_nleaves], 0,
		    (n - np->n_nleaves) * sizeof(*leaves));
		np->n_leaves = leaves;
		np->n_nleaves = n;
	}
	if (np->n_leaves[l] == NULL) {
		pg = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANON,
		    -1, 0);
		if (pg == MAP_FAILED) {
			*err = ENOSPC;
			return NULL;
		}
		np->n_leaves[l] = pg;		/* zero-filled: all holes */
	}
	pg = &np->n_leaves[l][idx % LEAF_PAGES];
	if (pg->p_id == 0 &&
	    (*err = page_alloc(np, rounddown(pos, PAGE_SIZE), pg)) != OK)
		return NULL;
	return pg;
}

/* Zero the bytes [start, end) of 'np', freeing the pages they cover whole. */
static void
zero_range(struct node *np, off_t start, off_t end)
{
	struct page *pg, *leaf;
	off_t pos, pend;
	size_t idx;

	for (pos = start; pos < end; pos = pend) {
		pend = MIN(rounddown(pos, PAGE_SIZE) + PAGE_SIZE, end);
		idx = (size_t)(pos / PAGE_SIZE);
		if (idx / LEAF_PAGES >= np->n_nleaves)
			break;
		if ((leaf = np->n_leaves[idx / LEAF_PAGES]) == NULL) {
			/* A whole leaf of holes. */
			pend = MIN((off_t)(idx / LEAF_PAGES + 1) * LEAF_PAGES *
			    PAGE_SIZE, end);
			continue;
		}
		pg = &leaf[idx % LEAF_PAGES];
		if (pg->p_id == 0)
			continue;
		if (pos % PAGE_SIZE == 0 && pend - pos == PAGE_SIZE)
			page_free(pg);
		else
			memset(page_addr(np, pos, pg) + pos % PAGE_SIZE, 0,
			    pend - pos);
	}
}

/* The end of what 'np's leaves can hold. */
static off_t
leaves_end(struct node *np)
{

	return (off_t)np->n_nleaves * LEAF_PAGES * PAGE_SIZE;
}

/*
 * Set the size of a regular file.  What lies beyond the old end in its last
 * page may have been written through a shared mapping: a file that grows must
 * read as zeroes there.
 */
static void
set_size(struct node *np, off_t size)
{
	off_t end;

	if (size < np->n_size) {
		/* Free every page past the new end, holes and all. */
		zero_range(np, size, leaves_end(np));
	} else if (size > np->n_size && np->n_size % PAGE_SIZE != 0) {
		end = MIN(roundup(np->n_size, PAGE_SIZE), size);
		zero_range(np, np->n_size, end);
	}
	np->n_size = size;
}

/*
 * Nodes.
 */
static struct node *
node_alloc(mode_t mode, uid_t uid, gid_t gid, int *err)
{
	struct node *np, **p;
	size_t n;
	ino_t ino;

	if (nodes_used >= max_nodes) {
		*err = ENOSPC;
		return NULL;
	}
	if (nodes_used + ROOT_INO >= nr_nodes) {
		n = nr_nodes ? nr_nodes * 2 : 64;
		if ((p = realloc(nodes, n * sizeof(*p))) == NULL) {
			*err = ENOSPC;
			return NULL;
		}
		memset(&p[nr_nodes], 0, (n - nr_nodes) * sizeof(*p));
		nodes = p;
		nr_nodes = n;
	}
	for (ino = next_ino; nodes[ino] != NULL; )
		if (++ino >= nr_nodes)
			ino = ROOT_INO;
	if ((np = calloc(1, sizeof(*np))) == NULL) {
		*err = ENOSPC;
		return NULL;
	}
	np->n_ino = ino;
	np->n_mode = mode;
	np->n_uid = uid;
	np->n_gid = gid;
	now(&np->n_atime);
	np->n_mtime = np->n_ctime = np->n_btime = np->n_atime;
	nodes[ino] = np;
	nodes_used++;
	next_ino = (ino + 1 < nr_nodes) ? ino + 1 : ROOT_INO;
	return np;
}

static void
node_free(struct node *np)
{
	size_t i;

	if (S_ISREG(np->n_mode)) {
		zero_range(np, 0, leaves_end(np));
		for (i = 0; i < np->n_nleaves; i++)
			if (np->n_leaves[i] != NULL)
				munmap(np->n_leaves[i], PAGE_SIZE);
		free(np->n_leaves);
	} else if (S_ISDIR(np->n_mode)) {
		for (i = 0; i < np->n_ndents; i++)
			free(np->n_dents[i].d_name);
		free(np->n_dents);
	}
	free(np->n_link);
	nodes[np->n_ino] = NULL;
	nodes_used--;
	free(np);
}

/* A node is gone once it has neither links nor references. */
static void
node_check(struct node *np)
{

	if (np->n_nlink == 0 && np->n_refs == 0)
		node_free(np);
}

static void
node_get(struct node *np, struct fsdriver_node *fn)
{

	np->n_refs++;
	fn->fn_ino_nr = np->n_ino;
	fn->fn_mode = np->n_mode;
	fn->fn_size = np->n_size;
	fn->fn_uid = np->n_uid;
	fn->fn_gid = np->n_gid;
	fn->fn_dev = np->n_rdev;
}

/*
 * Directories.
 */
static struct dent *
dir_find(struct node *dp, const char *name)
{
	size_t i;

	for (i = 0; i < dp->n_ndents; i++)
		if (dp->n_dents[i].d_name != NULL &&
		    !strcmp(dp->n_dents[i].d_name, name))
			return &dp->n_dents[i];
	return NULL;
}

static int
dir_add(struct node *dp, const char *name, ino_t ino)
{
	struct dent *d;
	size_t i, n;
	char *s;

	if ((s = strdup(name)) == NULL)
		return ENOSPC;
	for (i = 0; i < dp->n_ndents; i++)
		if (dp->n_dents[i].d_name == NULL)
			break;
	if (i == dp->n_ndents) {
		n = dp->n_ndents ? dp->n_ndents * 2 : 8;
		if ((d = realloc(dp->n_dents, n * sizeof(*d))) == NULL) {
			free(s);
			return ENOSPC;
		}
		memset(&d[dp->n_ndents], 0, (n - dp->n_ndents) * sizeof(*d));
		dp->n_dents = d;
		dp->n_ndents = n;
	}
	dp->n_dents[i].d_name = s;
	dp->n_dents[i].d_ino = ino;
	dp->n_used++;
	dp->n_size = dp->n_used;
	now(&dp->n_mtime);
	dp->n_ctime = dp->n_mtime;
	return OK;
}

/* Remove an entry; the slot stays, so that getdents positions do too. */
static void
dir_del(struct node *dp, struct dent *d)
{

	free(d->d_name);
	d->d_name = NULL;
	dp->n_used--;
	dp->n_size = dp->n_used;
	now(&dp->n_mtime);
	dp->n_ctime = dp->n_mtime;
}

/* A directory for 'name' in 'dp', which must not exist yet. */
static int
check_new(struct node *dp, const char *name)
{

	if (dp == NULL || !S_ISDIR(dp->n_mode))
		return ENOTDIR;
	if (dp->n_nlink == 0)
		return ENOENT;		/* removed while in use */
	if (dir_find(dp, name) != NULL)
		return EEXIST;
	return OK;
}

/* Create a node in 'dp' under 'name'; on success, linked once. */
static int
make_node(struct node *dp, const char *name, mode_t mode, uid_t uid,
	gid_t gid, struct node **npp)
{
	struct node *np;
	int r;

	if ((r = check_new(dp, name)) != OK)
		return r;
	if ((np = node_alloc(mode, uid, gid, &r)) == NULL)
		return r;
	if ((r = dir_add(dp, name, np->n_ino)) != OK) {
		node_free(np);
		return r;
	}
	np->n_nlink = 1;
	*npp = np;
	return OK;
}

/*
 * The file system calls.
 */
static int
tmpfs_mount(dev_t dev, unsigned int __unused flags,
	struct fsdriver_node *root, unsigned int *res_flags)
{
	struct node *np;
	int r;

	fs_dev = dev;
	if ((np = node_alloc(S_IFDIR | (opt_mode & ALLPERMS), opt_uid,
	    opt_gid, &r)) == NULL)
		return r;
	assert(np->n_ino == ROOT_INO);
	np->n_nlink = 2;
	np->n_parent = ROOT_INO;
	node_get(np, root);
	*res_flags = RES_64BIT | RES_HASPEEK | RES_HASMAPWRITE;
	return OK;
}

static void
tmpfs_unmount(void)
{
	size_t i;

	for (i = ROOT_INO; i < nr_nodes; i++)
		if (nodes[i] != NULL)
			node_free(nodes[i]);
	(void)vm_clear_cache(fs_dev);
}

static int
tmpfs_lookup(ino_t dir_nr, char *name, struct fsdriver_node *fn,
	int *is_mountpt)
{
	struct node *dp, *np;
	struct dent *d;

	if ((dp = get_node(dir_nr)) == NULL)
		return EINVAL;
	if (!S_ISDIR(dp->n_mode))
		return ENOTDIR;
	if (!strcmp(name, "."))
		np = dp;
	else if (dp->n_nlink == 0)
		return ENOENT;		/* removed: its parent may be gone */
	else if (!strcmp(name, ".."))
		np = get_node(dp->n_parent);
	else if ((d = dir_find(dp, name)) != NULL)
		np = get_node(d->d_ino);
	else
		return ENOENT;
	if (np == NULL)
		return ENOENT;
	node_get(np, fn);
	*is_mountpt = np->n_mountpt;
	return OK;
}

static int
tmpfs_putnode(ino_t ino_nr, unsigned int count)
{
	struct node *np;

	if ((np = get_node(ino_nr)) == NULL || count > np->n_refs) {
		printf("tmpfs: putnode of %llu by %u\n",
		    (unsigned long long)ino_nr, count);
		return EINVAL;
	}
	np->n_refs -= count;
	if (np->n_refs == 0) {
		np->n_mountpt = FALSE;
		node_check(np);
	}
	return OK;
}

static ssize_t
tmpfs_read(ino_t ino_nr, struct fsdriver_data *data, size_t bytes,
	off_t pos, int __unused call)
{
	struct node *np;
	struct page *pg;
	size_t done, chunk;
	int r;

	if ((np = get_node(ino_nr)) == NULL)
		return EINVAL;
	if (S_ISDIR(np->n_mode))
		return EISDIR;
	if (!S_ISREG(np->n_mode))
		return EINVAL;
	if (pos >= np->n_size)
		return 0;
	bytes = MIN(bytes, (size_t)(np->n_size - pos));

	for (done = 0; done < bytes; done += chunk, pos += chunk) {
		chunk = MIN(bytes - done, PAGE_SIZE - pos % PAGE_SIZE);
		if ((pg = page_find(np, pos)) != NULL)
			r = fsdriver_copyout(data, done,
			    page_addr(np, pos, pg) + pos % PAGE_SIZE, chunk);
		else
			r = fsdriver_zero(data, done, chunk);
		if (r != OK)
			return done > 0 ? (ssize_t)done : r;
	}
	now(&np->n_atime);
	return done;
}

static ssize_t
tmpfs_write(ino_t ino_nr, struct fsdriver_data *data, size_t bytes,
	off_t pos, int __unused call)
{
	struct node *np;
	struct page *pg;
	size_t done, chunk;
	int r;

	if ((np = get_node(ino_nr)) == NULL)
		return EINVAL;
	if (S_ISDIR(np->n_mode))
		return EISDIR;
	if (!S_ISREG(np->n_mode))
		return EINVAL;
	if (bytes == 0)
		return 0;
	if (pos > np->n_size)
		set_size(np, pos);	/* zero the old last page's tail */

	for (done = 0; done < bytes; done += chunk, pos += chunk) {
		chunk = MIN(bytes - done, PAGE_SIZE - pos % PAGE_SIZE);
		if ((pg = page_get(np, pos, &r)) == NULL ||
		    (r = fsdriver_copyin(data, done,
		    page_addr(np, pos, pg) + pos % PAGE_SIZE, chunk)) != OK) {
			if (done == 0)
				return r;
			break;
		}
		if (pos + (off_t)chunk > np->n_size)
			np->n_size = pos + chunk;
	}
	now(&np->n_mtime);
	np->n_ctime = np->n_mtime;
	return done;
}

/*
 * VM wants the pages for [pos, pos + bytes) in its cache: they already are,
 * except for holes, which get pages now.  Nothing past the end of the file.
 */
static ssize_t
tmpfs_peek(ino_t ino_nr, struct fsdriver_data * __unused data, size_t bytes,
	off_t pos, int __unused call)
{
	struct node *np;
	off_t p, end;
	int r;

	if ((np = get_node(ino_nr)) == NULL)
		return EINVAL;
	if (!S_ISREG(np->n_mode))
		return EINVAL;
	if (pos >= np->n_size)
		return 0;
	end = MIN(pos + (off_t)bytes, np->n_size);
	for (p = rounddown(pos, PAGE_SIZE); p < end; p += PAGE_SIZE)
		if (page_get(np, p, &r) == NULL)
			return (p > pos) ? p - pos : r;
	now(&np->n_atime);
	return end - pos;
}

/* Processes are about to write through shared mappings: as for peek. */
static int
tmpfs_mapwrite(ino_t ino_nr, size_t bytes, off_t pos)
{
	ssize_t r;

	if ((r = tmpfs_peek(ino_nr, NULL, bytes, pos, FSC_MAPWRITE)) < 0)
		return r;
	return (r > 0) ? OK : ENXIO;	/* ENXIO: at or beyond the end */
}

static ssize_t
tmpfs_getdents(ino_t ino_nr, struct fsdriver_data *data, size_t bytes,
	off_t *posp)
{
	struct fsdriver_dentry fd;
	struct node *dp, *np;
	struct dent *d;
	char buf[sizeof(struct dirent) + NAME_MAX + 1];
	off_t pos;
	ssize_t r;

	if ((dp = get_node(ino_nr)) == NULL)
		return EINVAL;
	if (!S_ISDIR(dp->n_mode))
		return ENOTDIR;

	fsdriver_dentry_init(&fd, data, bytes, buf, sizeof(buf));

	/* Position 0 is ".", 1 is "..", and 2 + i is slot i. */
	for (pos = *posp; pos < 2 + (off_t)dp->n_ndents; pos++) {
		if (pos == 0)
			r = fsdriver_dentry_add(&fd, dp->n_ino, ".", 1, DT_DIR);
		else if (pos == 1)
			r = fsdriver_dentry_add(&fd, dp->n_parent, "..", 2,
			    DT_DIR);
		else {
			d = &dp->n_dents[pos - 2];
			if (d->d_name == NULL ||
			    (np = get_node(d->d_ino)) == NULL)
				continue;
			r = fsdriver_dentry_add(&fd, d->d_ino, d->d_name,
			    strlen(d->d_name), IFTODT(np->n_mode));
		}
		if (r < 0)
			return r;
		if (r == 0)
			break;		/* no more room */
	}

	if ((r = fsdriver_dentry_finish(&fd)) >= 0) {
		*posp = pos;
		now(&dp->n_atime);
	}
	return r;
}

static int
tmpfs_trunc(ino_t ino_nr, off_t start, off_t end)
{
	struct node *np;

	if ((np = get_node(ino_nr)) == NULL)
		return EINVAL;
	if (!S_ISREG(np->n_mode))
		return EINVAL;

	if (end == 0)
		set_size(np, start);
	else if (start < np->n_size)
		zero_range(np, start, MIN(end, np->n_size));

	now(&np->n_mtime);
	np->n_ctime = np->n_mtime;
	return OK;
}

static int
tmpfs_create(ino_t dir_nr, char *name, mode_t mode, uid_t uid, gid_t gid,
	struct fsdriver_node *fn)
{
	struct node *np;
	int r;

	if ((r = make_node(get_node(dir_nr), name, mode, uid, gid,
	    &np)) != OK)
		return r;
	node_get(np, fn);
	return OK;
}

static int
tmpfs_mkdir(ino_t dir_nr, char *name, mode_t mode, uid_t uid, gid_t gid)
{
	struct node *dp, *np;
	int r;

	dp = get_node(dir_nr);
	if (dp != NULL && dp->n_nlink >= LINK_MAX)
		return EMLINK;
	if ((r = make_node(dp, name, mode, uid, gid, &np)) != OK)
		return r;
	np->n_nlink = 2;
	np->n_parent = dp->n_ino;
	dp->n_nlink++;
	return OK;
}

static int
tmpfs_mknod(ino_t dir_nr, char *name, mode_t mode, uid_t uid, gid_t gid,
	dev_t rdev)
{
	struct node *np;
	int r;

	if ((r = make_node(get_node(dir_nr), name, mode, uid, gid,
	    &np)) != OK)
		return r;
	np->n_rdev = rdev;
	return OK;
}

static int
tmpfs_link(ino_t dir_nr, char *name, ino_t ino_nr)
{
	struct node *dp, *np;
	int r;

	if ((np = get_node(ino_nr)) == NULL)
		return EINVAL;
	if (S_ISDIR(np->n_mode))
		return EPERM;
	if (np->n_nlink >= LINK_MAX)
		return EMLINK;
	dp = get_node(dir_nr);
	if ((r = check_new(dp, name)) != OK ||
	    (r = dir_add(dp, name, np->n_ino)) != OK)
		return r;
	np->n_nlink++;
	now(&np->n_ctime);
	return OK;
}

/* Is directory 'np' empty? */
static int
dir_empty(struct node *np)
{

	return np->n_used == 0;
}

static int
tmpfs_unlink(ino_t dir_nr, char *name, int call)
{
	struct node *dp, *np;
	struct dent *d;

	if ((dp = get_node(dir_nr)) == NULL || !S_ISDIR(dp->n_mode))
		return ENOTDIR;
	if ((d = dir_find(dp, name)) == NULL ||
	    (np = get_node(d->d_ino)) == NULL)
		return ENOENT;
	if (np->n_mountpt)
		return EBUSY;

	if (call == FSC_UNLINK) {
		if (S_ISDIR(np->n_mode))
			return EPERM;
	} else {
		if (!S_ISDIR(np->n_mode))
			return ENOTDIR;
		if (!dir_empty(np))
			return ENOTEMPTY;
	}

	dir_del(dp, d);
	if (S_ISDIR(np->n_mode)) {
		np->n_nlink = 0;
		dp->n_nlink--;
	} else
		np->n_nlink--;
	now(&np->n_ctime);
	node_check(np);
	return OK;
}

static int
tmpfs_rename(ino_t odir_nr, char *oname, ino_t ndir_nr, char *nname)
{
	struct node *odp, *ndp, *op, *np, *p;
	struct dent *od, *nd;
	int odir, r;

	if ((odp = get_node(odir_nr)) == NULL || !S_ISDIR(odp->n_mode) ||
	    (ndp = get_node(ndir_nr)) == NULL || !S_ISDIR(ndp->n_mode))
		return ENOTDIR;
	if ((od = dir_find(odp, oname)) == NULL ||
	    (op = get_node(od->d_ino)) == NULL)
		return ENOENT;
	if (ndp->n_nlink == 0)
		return ENOENT;
	if (op->n_mountpt)
		return EBUSY;
	odir = S_ISDIR(op->n_mode);

	/* A directory may not move into itself or below. */
	if (odir && odp != ndp)
		for (p = ndp; ; p = get_node(p->n_parent)) {
			if (p == op)
				return EINVAL;
			if (p == NULL || p->n_ino == ROOT_INO)
				break;
		}

	np = NULL;
	if ((nd = dir_find(ndp, nname)) != NULL) {
		np = get_node(nd->d_ino);
		assert(np != NULL);
		if (np == op)
			return OK;	/* the same file: nothing to do */
		if (np->n_mountpt)
			return EBUSY;
		if (odir && !S_ISDIR(np->n_mode))
			return ENOTDIR;
		if (!odir && S_ISDIR(np->n_mode))
			return EISDIR;
		if (S_ISDIR(np->n_mode) && !dir_empty(np))
			return ENOTEMPTY;
	} else if (odir && odp != ndp && ndp->n_nlink >= LINK_MAX)
		return EMLINK;

	/* Nothing can fail from here on, but adding the new entry. */
	if (nd != NULL) {
		nd->d_ino = op->n_ino;
		now(&ndp->n_mtime);
		ndp->n_ctime = ndp->n_mtime;
		if (S_ISDIR(np->n_mode)) {
			np->n_nlink = 0;
			ndp->n_nlink--;
		} else
			np->n_nlink--;
		now(&np->n_ctime);
		node_check(np);
	} else if ((r = dir_add(ndp, nname, op->n_ino)) != OK)
		return r;

	/* dir_add() may have moved the old directory's entries. */
	od = dir_find(odp, oname);
	assert(od != NULL && od->d_ino == op->n_ino);
	dir_del(odp, od);

	if (odir && odp != ndp) {
		op->n_parent = ndp->n_ino;
		odp->n_nlink--;
		ndp->n_nlink++;
	}
	now(&op->n_ctime);
	return OK;
}

static int
tmpfs_slink(ino_t dir_nr, char *name, uid_t uid, gid_t gid,
	struct fsdriver_data *data, size_t bytes)
{
	struct node *np;
	char *s;
	int r;

	if (bytes >= PATH_MAX)
		return ENAMETOOLONG;
	if ((s = malloc(bytes + 1)) == NULL)
		return ENOSPC;
	if ((r = fsdriver_copyin(data, 0, s, bytes)) != OK) {
		free(s);
		return r;
	}
	s[bytes] = '\0';
	if ((r = make_node(get_node(dir_nr), name, S_IFLNK | ACCESSPERMS,
	    uid, gid, &np)) != OK) {
		free(s);
		return r;
	}
	np->n_link = s;
	np->n_size = bytes;
	return OK;
}

static ssize_t
tmpfs_rdlink(ino_t ino_nr, struct fsdriver_data *data, size_t bytes)
{
	struct node *np;
	int r;

	if ((np = get_node(ino_nr)) == NULL)
		return EINVAL;
	if (!S_ISLNK(np->n_mode))
		return EINVAL;
	bytes = MIN(bytes, (size_t)np->n_size);
	if ((r = fsdriver_copyout(data, 0, np->n_link, bytes)) != OK)
		return r;
	now(&np->n_atime);
	return bytes;
}

static int
tmpfs_stat(ino_t ino_nr, struct stat *st)
{
	struct node *np;
	size_t i, n;

	if ((np = get_node(ino_nr)) == NULL)
		return EINVAL;

	for (i = n = 0; i < np->n_nleaves * LEAF_PAGES; i++)
		if (np->n_leaves[i / LEAF_PAGES] != NULL &&
		    np->n_leaves[i / LEAF_PAGES][i % LEAF_PAGES].p_id != 0)
			n++;

	st->st_dev = fs_dev;
	st->st_ino = np->n_ino;
	st->st_mode = np->n_mode;
	st->st_nlink = np->n_nlink;
	st->st_uid = np->n_uid;
	st->st_gid = np->n_gid;
	st->st_rdev = np->n_rdev;
	st->st_size = np->n_size;
	st->st_atimespec = np->n_atime;
	st->st_mtimespec = np->n_mtime;
	st->st_ctimespec = np->n_ctime;
	st->st_birthtimespec = np->n_btime;
	st->st_blksize = PAGE_SIZE;
	st->st_blocks = (blkcnt_t)n * (PAGE_SIZE / S_BLKSIZE);
	return OK;
}

static int
tmpfs_chown(ino_t ino_nr, uid_t uid, gid_t gid, mode_t *mode)
{
	struct node *np;

	if ((np = get_node(ino_nr)) == NULL)
		return EINVAL;
	np->n_uid = uid;
	np->n_gid = gid;
	np->n_mode &= ~(S_ISUID | S_ISGID);
	now(&np->n_ctime);
	*mode = np->n_mode;
	return OK;
}

static int
tmpfs_chmod(ino_t ino_nr, mode_t *mode)
{
	struct node *np;

	if ((np = get_node(ino_nr)) == NULL)
		return EINVAL;
	np->n_mode = (np->n_mode & ~ALLPERMS) | (*mode & ALLPERMS);
	now(&np->n_ctime);
	*mode = np->n_mode;
	return OK;
}

static int
tmpfs_utime(ino_t ino_nr, struct timespec *atime, struct timespec *mtime)
{
	struct node *np;
	struct timespec t;

	if ((np = get_node(ino_nr)) == NULL)
		return EINVAL;
	now(&t);
	if (atime->tv_nsec == UTIME_NOW)
		np->n_atime = t;
	else if (atime->tv_nsec != UTIME_OMIT)
		np->n_atime = *atime;
	if (mtime->tv_nsec == UTIME_NOW)
		np->n_mtime = t;
	else if (mtime->tv_nsec != UTIME_OMIT)
		np->n_mtime = *mtime;
	np->n_ctime = t;
	return OK;
}

static int
tmpfs_mountpt(ino_t ino_nr)
{
	struct node *np;

	if ((np = get_node(ino_nr)) == NULL)
		return EINVAL;
	if (np->n_mountpt)
		return EBUSY;
	if (S_ISREG(np->n_mode) || S_ISBLK(np->n_mode))
		return ENOTDIR;
	np->n_mountpt = TRUE;
	return OK;
}

static int
tmpfs_statvfs(struct statvfs *st)
{

	st->f_flag = ST_NOTRUNC;
	st->f_bsize = st->f_frsize = st->f_iosize = PAGE_SIZE;
	st->f_blocks = max_pages;
	st->f_bfree = st->f_bavail = max_pages - pages_used;
	st->f_files = max_nodes;
	st->f_ffree = st->f_favail = max_nodes - nodes_used;
	st->f_namemax = NAME_MAX;
	return OK;
}

static void
tmpfs_sync(void)
{

	/* Nothing to write out. */
}

static struct fsdriver tmpfs_table = {
	.fdr_mount	= tmpfs_mount,
	.fdr_unmount	= tmpfs_unmount,
	.fdr_lookup	= tmpfs_lookup,
	.fdr_putnode	= tmpfs_putnode,
	.fdr_read	= tmpfs_read,
	.fdr_write	= tmpfs_write,
	.fdr_peek	= tmpfs_peek,
	.fdr_mapwrite	= tmpfs_mapwrite,
	.fdr_getdents	= tmpfs_getdents,
	.fdr_trunc	= tmpfs_trunc,
	.fdr_create	= tmpfs_create,
	.fdr_mkdir	= tmpfs_mkdir,
	.fdr_mknod	= tmpfs_mknod,
	.fdr_link	= tmpfs_link,
	.fdr_unlink	= tmpfs_unlink,
	.fdr_rmdir	= tmpfs_unlink,
	.fdr_rename	= tmpfs_rename,
	.fdr_slink	= tmpfs_slink,
	.fdr_rdlink	= tmpfs_rdlink,
	.fdr_stat	= tmpfs_stat,
	.fdr_chown	= tmpfs_chown,
	.fdr_chmod	= tmpfs_chmod,
	.fdr_utime	= tmpfs_utime,
	.fdr_mountpt	= tmpfs_mountpt,
	.fdr_statvfs	= tmpfs_statvfs,
	.fdr_sync	= tmpfs_sync,
};

/* "size=" in bytes, with an optional k, m or g. */
static size_t
parse_size(const char *s, size_t def)
{
	unsigned long n;
	char *end;

	if (*s == '\0')
		return def;
	n = strtoul(s, &end, 10);
	switch (tolower((unsigned char)*end)) {
	case 'g': n *= 1024;	/* FALLTHROUGH */
	case 'm': n *= 1024;	/* FALLTHROUGH */
	case 'k': n *= 1024;	break;
	case '\0': break;
	default: return def;
	}
	n /= PAGE_SIZE;
	return (n > 0 && n <= SIZE_MAX) ? (size_t)n : def;
}

static int
sef_cb_init_fresh(int __unused type, sef_init_info_t * __unused info)
{
	struct vm_stats_info vsi;
	size_t half;
	int i;

	for (i = 1; i < env_argc - 1; i++)
		if (!strcmp(env_argv[i], "-o"))
			optset_parse(optset_table, env_argv[++i]);

	if (vm_info_stats(&vsi) != OK)
		panic("tmpfs: cannot get the memory size");
	half = vsi.vsi_total / 2 * (vsi.vsi_pagesize / PAGE_SIZE);
	max_pages = parse_size(opt_size, half);
	max_nodes = (opt_nr_inodes > 0) ? (size_t)opt_nr_inodes : max_pages;
	return OK;
}

static void
sef_cb_signal_handler(int signo)
{

	if (signo == SIGTERM)
		fsdriver_terminate();
}

int
main(int argc, char *argv[])
{

	env_setargs(argc, argv);
	sef_setcb_init_fresh(sef_cb_init_fresh);
	sef_setcb_signal_handler(sef_cb_signal_handler);
	sef_startup();

	fsdriver_task(&tmpfs_table);

	return EXIT_SUCCESS;
}
