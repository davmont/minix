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
int fat_set(uint32_t cn, uint32_t val);
int chain_nth(uint32_t start, int contig, uint64_t frcn, uint32_t *cnp);
int chain_read(uint32_t start, int contig, uint64_t off, void *buf, size_t len);
int chain_write(uint32_t start, int contig, uint64_t off, const void *buf,
	size_t len);
int bmap(struct inode *rip, uint64_t frcn, uint64_t *secp);
uint64_t cluster_to_sector(uint32_t cn);
int bitmap_count_free(void);
int bitmap_set(uint32_t cn);
int bitmap_clear(uint32_t cn);
int cluster_alloc(uint32_t prev, uint32_t *newcn);
int free_chain(uint32_t startcn, int contig, uint64_t nclusters);
int convert_to_chained(struct inode *rip);
uint64_t chain_clusters(uint32_t start, int contig, uint64_t bytes);

/* write.c */
time_t exfat_now(void);
int update_direntry(struct inode *rip);
int extend_file(struct inode *rip, uint64_t newsize);
int zero_cluster(uint32_t cn);
int fs_create(ino_t dir_nr, char *name, mode_t mode, uid_t uid, gid_t gid,
	struct fsdriver_node *node);
int fs_mkdir(ino_t dir_nr, char *name, mode_t mode, uid_t uid, gid_t gid);
int fs_unlink(ino_t dir_nr, char *name, int call);
int fs_rename(ino_t old_dir_nr, char *old_name, ino_t new_dir_nr,
	char *new_name);
int fs_trunc(ino_t ino_nr, off_t start, off_t end);
int fs_utime(ino_t ino_nr, struct timespec *atime, struct timespec *mtime);
int fs_chmod(ino_t ino_nr, mode_t *mode);
void set_volume_dirty(int dirty);

/* dir.c */
int fs_lookup(ino_t dir_nr, char *name, struct fsdriver_node *node,
	int *is_mountpt);
ssize_t fs_getdents(ino_t ino_nr, struct fsdriver_data *data, size_t bytes,
	off_t *pos);
int scan_root_meta(void);
int dir_find(struct inode *dir, const char *name, uint64_t *offp,
	struct exfat_file_entry *fe, struct exfat_stream_entry *se);
int dir_is_empty(struct inode *dir);
unsigned set_entries_of(const struct exfat_file_entry *fe);

/* name.c */
int upcase_init(void);
void upcase_free(void);
uint16_t upcase_one(uint16_t wc);
uint16_t name_hash(const uint16_t *name, int len);
int utf16_to_utf8(const uint16_t *in, int inlen, char *out, size_t outsize);
int utf8_to_utf16(const char *in, uint16_t *out, int outmax);
void exfat_to_timespec(uint32_t stamp, uint8_t inc10ms, uint8_t tzoff,
	struct timespec *tsp);
void timespec_to_exfat(time_t sec, uint32_t *stamp, uint8_t *inc10ms,
	uint8_t *tzoff);

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
