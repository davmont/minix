#ifndef _EXFAT_TYPE_H_
#define _EXFAT_TYPE_H_

/*
 * In-core mount state for one mounted exFAT file system.  All "sector" numbers
 * are device sector numbers; libminixfs is set to a block size equal to the
 * sector size at mount time, so a sector number is also an lmfs block number.
 */
struct exfat_mount {
	dev_t pm_dev;			/* block device mounted */
	uid_t pm_uid;			/* uid to report for files */
	gid_t pm_gid;			/* gid to report for files */
	mode_t pm_mask;			/* andmask for file permissions */
	mode_t pm_dirmask;		/* andmask for directory permissions */
	int pm_rdonly;			/* TRUE if mounted read-only */

	/* Geometry. */
	unsigned pm_bytes_per_sec;	/* sector size in bytes */
	unsigned pm_sec_shift;		/* log2(bytes per sector) */
	unsigned pm_sec_per_clus;	/* sectors per cluster */
	unsigned pm_clus_shift;		/* log2(sectors per cluster) */
	unsigned long pm_bytes_per_clus; /* bytes per cluster */
	unsigned pm_clus_byte_shift;	/* log2(bytes per cluster) */

	uint64_t pm_fat_off;		/* first FAT, in sectors */
	uint64_t pm_fat_length;		/* sectors per FAT */
	uint64_t pm_cluster_heap_off;	/* first data sector, in sectors */
	uint32_t pm_cluster_count;	/* number of clusters in the heap */
	uint32_t pm_max_cluster;	/* highest valid cluster number */
	uint32_t pm_root_cluster;	/* first cluster of the root directory */
	unsigned pm_num_fats;		/* 1 (or 2 for TexFAT) */

	/* Allocation bitmap (1 bit per cluster, cluster 2 == bit 0). */
	uint32_t pm_bitmap_cluster;	/* first cluster of the bitmap */
	uint64_t pm_bitmap_length;	/* bitmap size in bytes */
	unsigned long pm_free_clusters;	/* number of free clusters */
	uint32_t pm_next_free;		/* next-free-cluster allocation hint */

	/* Up-case table: location on disk, then the loaded code-unit map. */
	uint32_t pm_upcase_cluster;	/* first cluster of the up-case table */
	uint64_t pm_upcase_length;	/* up-case table size in bytes */
	uint16_t *pm_upcase;		/* code-unit -> up-cased, 65536 entries */
};

/*
 * In-core node.  An exFAT file/dir is identified by the location of its File
 * directory entry: the first cluster of the containing directory plus the byte
 * offset of the entry within that directory's data stream.  The root directory
 * has no such entry and uses the synthetic key ROOT_INODE.
 */
struct inode {
	ino_t i_num;			/* synthetic inode number (VFS handle) */
	int i_count;			/* reference count; 0 == free slot */
	dev_t i_dev;			/* device (== mounted fs) */

	uint32_t i_dirclust;		/* first cluster of containing dir */
	uint32_t i_diroffset;		/* byte offset of File entry in that dir */
	uint8_t i_secondary_count;	/* secondary entries in our set */

	/* Parent identity, recorded at lookup time so that ".." can be resolved
	 * (exFAT has no on-disk "." / ".." entries).  i_dirclust is the parent's
	 * first data cluster. */
	ino_t i_parent;			/* inode number of the parent directory */
	uint8_t i_parent_secflags;	/* parent's stream flags (FAT-chain bit) */

	uint32_t i_start;		/* first cluster of the file's data */
	uint64_t i_size;		/* data_length (allocated size) bytes */
	uint64_t i_valid;		/* valid_data_length bytes */
	uint16_t i_attrs;		/* exFAT attribute bits */
	uint8_t i_secflags;		/* stream GeneralSecondaryFlags */

	mode_t i_mode;			/* derived st_mode */
	time_t i_mtime;			/* modification time */
	time_t i_atime;			/* access time */
	time_t i_ctime;			/* creation time */

	int i_root;			/* TRUE if this is the root directory */
	int i_mountpoint;		/* TRUE if a fs is mounted here */
};

/* A file whose stream entry has the NoFatChain flag occupies a contiguous run
 * of clusters and the FAT must not be consulted for it. */
#define IS_CONTIG(rip)	((rip)->i_secflags & EXFAT_SECFLAG_NO_FAT_CHAIN)

#endif /* _EXFAT_TYPE_H_ */
