#ifndef _VFAT_PROTO_H_
#define _VFAT_PROTO_H_

#include <minix/fsdriver.h>

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
struct inode *enter_inode(unsigned long dirclust, unsigned long diroffset,
	const struct direntry *dp);
struct inode *get_root_inode(void);
void node_to_mode(struct inode *rip);
ino_t make_ino(unsigned long dirclust, unsigned long diroffset);
int fs_putnode(ino_t ino_nr, unsigned int count);

/* fat.c */
int bmap(struct inode *rip, unsigned long frcn, unsigned long *bnp,
	unsigned long *cnp, unsigned long *sizep);
int chain_nth(unsigned long start, unsigned long frcn, unsigned long *cnp);
int fatentry_get(unsigned long cn, unsigned long *outcn);
void fc_init(struct inode *rip);
int fat_set(unsigned long cn, unsigned long val);
int fill_inusemap(void);
int cluster_alloc(unsigned long prev, unsigned long *newcn);
int free_chain(unsigned long startcn);
unsigned long entry_sector(unsigned long dirclust, unsigned long diroffset);

/* write.c */
int update_direntry(struct inode *rip);
int extend_file(struct inode *rip, uint32_t newsize);
int zero_cluster(unsigned long cn);
int fs_trunc(ino_t ino_nr, off_t start, off_t end);
int fs_utime(ino_t ino_nr, struct timespec *atime, struct timespec *mtime);
int fs_chmod(ino_t ino_nr, mode_t *mode);
void fs_sync(void);
void update_fsinfo(void);
time_t vfat_now(void);

/* direntry.c */
int fs_lookup(ino_t dir_nr, char *name, struct fsdriver_node *node,
	int *is_mountpt);
ssize_t fs_getdents(ino_t ino_nr, struct fsdriver_data *data, size_t bytes,
	off_t *pos);
int fs_create(ino_t dir_nr, char *name, mode_t mode, uid_t uid, gid_t gid,
	struct fsdriver_node *node);
int fs_mkdir(ino_t dir_nr, char *name, mode_t mode, uid_t uid, gid_t gid);
int fs_unlink(ino_t dir_nr, char *name, int call);
int fs_rename(ino_t old_dir_nr, char *old_name, ino_t new_dir_nr,
	char *new_name);

/* name.c */
void unix2dostime(time_t t, uint16_t *ddp, uint16_t *dtp);
int dos2unixfn(const unsigned char dn[11], unsigned char *un, int lower);
int win2unixfn(const struct winentry *wep, unsigned char *un, size_t unsize,
	int chksum);
uint8_t winchksum(const unsigned char *name);
void dos2unixtime(unsigned int dd, unsigned int dt, struct timespec *tsp);

/* read.c */
ssize_t fs_readwrite(ino_t ino_nr, struct fsdriver_data *data, size_t bytes,
	off_t pos, int call);
ssize_t fs_peek(ino_t ino_nr, struct fsdriver_data *data, size_t bytes,
	off_t pos, int call);

/* stadir.c */
int fs_stat(ino_t ino_nr, struct stat *statbuf);
int fs_statvfs(struct statvfs *st);

/* utility.c */
struct buf *get_block(dev_t dev, block64_t block, int how);
void put_block(struct buf *bp);

#endif /* _VFAT_PROTO_H_ */
