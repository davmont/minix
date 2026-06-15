#ifndef _VFAT_TYPE_H_
#define _VFAT_TYPE_H_

#include "bpb.h"

/*
 * In-core mount state for one mounted FAT file system.  Field names follow
 * NetBSD's struct msdosfsmount (pm_*) so the geometry macros below can be
 * reused verbatim.  All "block numbers" (bn) are device sector numbers; the
 * libminixfs block size is set to the sector size at mount time, so a sector
 * number is also an lmfs block number.
 */
struct fat_mount {
	dev_t pm_dev;			/* block device mounted */
	uid_t pm_uid;			/* uid to report for files */
	gid_t pm_gid;			/* gid to report for files */
	mode_t pm_mask;			/* andmask for file permissions */
	mode_t pm_dirmask;		/* andmask for directory permissions */

	struct bpb50 pm_bpb;		/* BIOS parameter block */
	unsigned long pm_FATsecs;	/* actual number of FAT sectors */
	unsigned long pm_fatblk;	/* sector # of first FAT */
	unsigned long pm_rootdirblk;	/* sector # (cluster # for FAT32) of root */
	unsigned long pm_rootdirsize;	/* root dir size in sectors (FAT12/16) */
	unsigned long pm_firstcluster;	/* sector number of first data cluster */
	unsigned long pm_nmbrofclusters;/* # of clusters in filesystem */
	unsigned long pm_maxcluster;	/* maximum cluster number */
	unsigned long pm_cnshift;	/* file offset >> this == cluster number */
	unsigned long pm_crbomask;	/* file offset & this == cluster rel off */
	unsigned long pm_bnshift;	/* file offset >> this == sector number */
	unsigned long pm_bpcluster;	/* bytes per cluster */
	unsigned long pm_fatblocksize;	/* size of FAT blocks in bytes */
	unsigned long pm_fatblocksec;	/* size of FAT blocks in sectors */
	unsigned long pm_fatsize;	/* size of FAT in bytes */
	unsigned long pm_fatmask;	/* mask to use for FAT numbers */
	unsigned long pm_fsinfo;	/* fsinfo block number */
	unsigned int pm_fatmult;	/* used in FAT offset computation */
	unsigned int pm_fatdiv;		/* used in FAT offset computation */
	unsigned int pm_curfat;		/* current FAT for FAT32 (0 otherwise) */
	unsigned int pm_flags;		/* see msdosfsmount option flags */
	int pm_rootmode;		/* mode bits for the root directory */
};

/* Shorthand for the BPB fields kept inside the mount. */
#define pm_BytesPerSec	pm_bpb.bpbBytesPerSec
#define pm_ResSectors	pm_bpb.bpbResSectors
#define pm_FATs		pm_bpb.bpbFATs
#define pm_RootDirEnts	pm_bpb.bpbRootDirEnts
#define pm_Sectors	pm_bpb.bpbSectors
#define pm_Media	pm_bpb.bpbMedia
#define pm_SecPerTrack	pm_bpb.bpbSecPerTrack
#define pm_Heads	pm_bpb.bpbHeads
#define pm_HiddenSects	pm_bpb.bpbHiddenSecs
#define pm_HugeSectors	pm_bpb.bpbHugeSectors

/* FAT byte offset for cluster cn. */
#define FATOFS(pmp, cn)	((cn) * (pmp)->pm_fatmult / (pmp)->pm_fatdiv)

/* Geometry conversions (copied from NetBSD msdosfsmount.h). */
#define de_bn2cn(pmp, bn) ((bn) >> ((pmp)->pm_cnshift - (pmp)->pm_bnshift))
#define de_cn2bn(pmp, cn) ((cn) << ((pmp)->pm_cnshift - (pmp)->pm_bnshift))
#define de_cluster(pmp, off) ((off) >> (pmp)->pm_cnshift)
#define de_clcount(pmp, size) \
	(((size) + (pmp)->pm_bpcluster - 1) >> (pmp)->pm_cnshift)
#define de_blk(pmp, off) (de_cn2bn(pmp, de_cluster((pmp), (off))))
#define de_cn2off(pmp, cn) ((cn) << (pmp)->pm_cnshift)
#define de_bn2off(pmp, bn) ((bn) << (pmp)->pm_bnshift)

/* Map a cluster number into a filesystem relative sector number. */
#define cntobn(pmp, cn) \
	(de_cn2bn((pmp), (cn) - CLUST_FIRST) + (pmp)->pm_firstcluster)

/* Sector number of root-dir entry at byte offset dirofs (FAT12/16). */
#define roottobn(pmp, dirofs) (de_blk((pmp), (dirofs)) + (pmp)->pm_rootdirblk)

/* Sector of a dir entry at cluster dirclu, byte offset dirofs. */
#define detobn(pmp, dirclu, dirofs) \
	((dirclu) == MSDOSFSROOT ? roottobn((pmp), (dirofs)) \
				 : cntobn((pmp), (dirclu)))

/*
 * Per-file FAT cluster cache, to avoid walking the chain from the start on
 * every bmap.  Mirrors NetBSD's struct fatcache.
 */
#define	FC_SIZE		2	/* number of cache slots per file */
#define	FC_LASTMAP	0	/* entry the last bmap() resolved */
#define	FC_LASTFC	1	/* entry for the last cluster in the file */
#define	FCE_EMPTY	0xffffffff	/* doesn't represent an actual cluster */

struct fatcache {
	unsigned long fc_frcn;	/* file relative cluster number */
	unsigned long fc_fsrcn;	/* filesystem relative cluster number */
};

/*
 * In-core node.  A FAT file/dir is identified by the location of its
 * directory entry: (de_dirclust, de_diroffset).  The root directory has no
 * such entry; it uses the synthetic key ROOT_INODE.
 */
struct inode {
	ino_t i_num;			/* synthetic inode number (VFS handle) */
	int i_count;			/* reference count; 0 == free slot */
	dev_t i_dev;			/* device (== mounted fs) */
	unsigned long i_dirclust;	/* cluster containing our dir entry */
	unsigned long i_diroffset;	/* byte offset of dir entry in that dir */
	unsigned long i_start;		/* first cluster of the file's data */
	uint32_t i_size;		/* file size in bytes (0 for dirs) */
	uint8_t i_attrs;		/* FAT attribute byte */
	mode_t i_mode;			/* derived st_mode */
	time_t i_mtime;			/* derived modification time */
	int i_root;			/* TRUE if this is the root directory */
	int i_mountpoint;		/* TRUE if a fs is mounted here */
	struct fatcache i_fc[FC_SIZE];	/* cluster chain cache */
};

#endif /* _VFAT_TYPE_H_ */
