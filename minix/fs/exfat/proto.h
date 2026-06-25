#ifndef _EXFAT_PROTO_H_
#define _EXFAT_PROTO_H_

#include <minix/fsdriver.h>

struct exfat_file_entry;
struct exfat_stream_entry;
struct inode;

/* mount.c */
int fs_mount(dev_t dev, unsigned int flags, struct fsdriver_node *root_node,
	unsigned int *res_flags);
void fs_unmount(void);
int fs_mountpt(ino_t ino_nr);

/* inode.c */
void init_inode_cache(void);
struct inode *get_inode(ino_t ino_nr);
struct inode *find_inode(ino_t ino_nr);
void put_inode(struct inode *rip);
struct inode *enter_inode(struct inode *dir, uint32_t diroffset,
	const struct exfat_file_entry *fe, const struct exfat_stream_entry *se);
struct inode *get_parent_inode(struct inode *rip);
struct inode *get_root_inode(void);
void node_to_mode(struct inode *rip);
ino_t make_ino(uint32_t dirclust, uint32_t diroffset);
int fs_putnode(ino_t ino_nr, unsigned int count);

/* cluster.c */
int fat_get(uint32_t cn, uint32_t *nextp);
int chain_nth(uint32_t start, int contig, uint64_t frcn, uint32_t *cnp);
int chain_read(uint32_t start, int contig, uint64_t off, void *buf, size_t len);
int bmap(struct inode *rip, uint64_t frcn, uint64_t *secp);
uint64_t cluster_to_sector(uint32_t cn);
int bitmap_count_free(void);

/* dir.c */
int fs_lookup(ino_t dir_nr, char *name, struct fsdriver_node *node,
	int *is_mountpt);
ssize_t fs_getdents(ino_t ino_nr, struct fsdriver_data *data, size_t bytes,
	off_t *pos);
int scan_root_meta(void);

/* name.c */
int upcase_init(void);
void upcase_free(void);
uint16_t upcase_one(uint16_t wc);
uint16_t name_hash(const uint16_t *name, int len);
int utf16_to_utf8(const uint16_t *in, int inlen, char *out, size_t outsize);
int utf8_to_utf16(const char *in, uint16_t *out, int outmax);
void exfat_to_timespec(uint32_t stamp, uint8_t inc10ms, uint8_t tzoff,
	struct timespec *tsp);

/* read.c */
ssize_t fs_readwrite(ino_t ino_nr, struct fsdriver_data *data, size_t bytes,
	off_t pos, int call);
ssize_t fs_peek(ino_t ino_nr, struct fsdriver_data *data, size_t bytes,
	off_t pos, int call);

/* stadir.c */
int fs_stat(ino_t ino_nr, struct stat *statbuf);
int fs_statvfs(struct statvfs *st);
void fs_sync(void);

/* utility.c */
struct buf *get_block(dev_t dev, block64_t block, int how);
void put_block(struct buf *bp);

#endif /* _EXFAT_PROTO_H_ */
